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
void KexDecor_Init(void);        // per level: load/verify authored scenery animations
int  KexDecor_ViewTile(int nSprite); // render-time frame, including per-instance phase
#define kKexFlashPal 30
#define kKexBlasterTile 7805
#define kKexAnubisFireTile 7806
#define kKexFireballTile 7807
#define kKexSelkisCobraTile 7847
#define kKexAmunBombTile 7848
#define kKexBoulderShardTile 7849
void KexFlash_Hit(int nSprite);     // the remaster's white blink on a hit creature
void KexFlash_RadialHit(int nSprite); // same mark, identifiable by the focused audit
int  KexFlash_Active(int nSprite);
void KexFlash_Tick(void);
int  KexBreak_IsProp(int nSprite);  // a breakable prop's sprite
void    KexVL_Load(char const *mapfile);   // the level's baked light field (KEX_<NAME>.LIT)
void    KexVL_Free(void);
int     KexVL_Ready(void);
int32_t KexVL_ShadeAt(float x, float y, float z, int32_t fallback);   // Build shade at a world point
int     KexVL_ColorAt(float x, float y, float z, float color[3]);     // authored RGB at a world point

// The remaster wav that is this Build sound, or NULL; *pLoop tells whether it
// loops (the flag stands in for the VOC loop block a WAV does not have).
char const *KexSound_Alias(char const *name, int *pLoop);
int KexSound_Get(char const *name);      // a Kex-only sound (kexsound.txt name with no VOC), -1 if none

// A_RandomItem: the remaster's enemy-death drop roll; no-op outside Kex mode.
int  KexDrop_ActorHeight(int nSprite);
int  KexGibDeath_OwnsDrop(int nStat);
void KexDrop_RandomItemAt(int nSector, int x, int y, int z, int nKiller);
void KexDrop_RandomItem(int nSprite, int nKiller, int actorHeight = -1);
void KexDrop_OrbSpawnPickup(int nEffectSprite); // final Fx_{Ammo,Health}Spawn callback

void KexTele_Init(void);         // per level: this map's teleport pads
void KexTele_Tick(void);         // per tick: step-on pads move the player
// kexHud draws teleport feedback as a separate green full-screen overlay.
// This returns its current source alpha (255 down to zero), never a classic
// palette-tint amount.
int  KexTele_FlashAlpha(void);
// EnterSectorSpecial() accepts all non-REMOTE_TRIGGER special sectors only
// while the player is within the Kex actor's 48-unit step height of its floor.
int KexPlayer_OnSectorFloor(int nPlayer, int nSector);

// Kex travel objects use the level-finish pickup path. Map_Tomb1 (0) and
// Map_Tomb2 (22) are the authored travel destinations outside the overworld.
void KexTravel_ChangeMap(int nMap);
// Apply a normal travel object's source-authored destination position before
// RestartPlayer builds the next map's puppet.  nAngle is that map's authored
// entry yaw, which the source preserves while moving the actor origin.
int  KexTravel_PrepareSpawn(int nPlayer, int nAngle);

// The sectors the level scripts move (the casket lids). The converter marks
// them with a lotag 64 sprite; the moves are the scripts', kept here.
void KexScripted_Reset(void);           // before the level's sprites are read
void KexScripted_Add(int nSector, int nTag, int nLinkedSector);
void KexScripted_Init(void);            // after: the scripts' load-time moves
void KexScripted_Tick(void);
void KexScripted_Trigger(int when);     // 0 = Ramses begins, 1 = Ramses done

// FLOOR_TRIGGER_PLATE is a small one-shot moving floor in Kex: it begins
// raised, sinks at two source units per 60 Hz update, then fires its tag.
// Its tag+1 can reverse the live plate before it reaches the bottom.
void KexPlate_Reset(void);
void KexPlate_Add(int nSector, int nTag, int nTarget);
void KexPlate_Tick(void);
void KexPlate_ChannelFired(int nChannel);

// REMOTE_TRIGGER is a walk-in dispatcher, not a Build touchplate. It resets
// tag+1 wall switches/live floor plates, fires its mapped tag and retires only
// when one of the remaster's one-shot mover targets was activated.
void KexRemote_Reset(void);
void KexRemote_Add(int nSector, int nTag, int nChannel, int bOneShot);
void KexRemote_Tick(void);

// Raw Kex switch tags survive as construction markers so floor plates can
// restore tag+1 switch art without firing that channel's other listeners.
void KexSwitch_Reset(void);
void KexSwitch_Add(int nSector, int nTag, int nOffPic, int nType, int x, int y);
void KexSwitch_ResetByTag(int nTag);
// Returns nonzero only for a remaster switch construction marker.  The Kex
// source plays one of two event-class-specific sounds from the user actor.
int  KexSwitch_PlaySound(int nWall, int nPlayer);

// DOOR_OPEN_ONLY_MESSAGE opens only from its event tag.  A direct use in the
// remaster stays sealed and prints its localized "$str_92" message.
void KexSealedDoor_Reset(void);
void KexSealedDoor_Add(int nSector);
int  KexSealedDoor_Use(int nSector);
void KexSealedDoor_Tick(void); // includes an env-only focused regression probe

// kexPlayer::TryUse dispatches only the remaster's direct-use events.  The
// converted map also contains contact, script and damage events as ordinary
// Build sectors, so they must not inherit DOS's generic use-key behaviour.
int  KexUse_WallAllowed(int nWall);
int  KexUse_SectorAllowed(int nSector);

// Remastered floating platforms are paired moving slabs: the event sector's
// ceiling and its parameter sector's floor, including every converted split
// piece. kexfloat.txt carries those surfaces and their exact mover variant.
void KexFloat_Reset(void);
void KexFloat_Init(void);
void KexFloat_Tick(void);
void KexPad_Reset(void);
void KexPad_Init(void);
void KexPad_Tick(void);
void KexPad_ChannelFired(int nChannel);

// The Scepter vase swarm, Eye of Ra player weapon (Proj_Ra_Fire), and Shawl
// cobra trio (Proj_Cobra_Green) are simulated as short-lived Kex-break
// runlist actors using the generated projectile tiles from kexdrop.txt.
void KexBouncyBalls_Spawn(int nSector, int x, int y, int z, int offset);
int  KexRaFire_Available(void);
int  KexRaFire_PlayerFire(short nSprite, int nPitch);
int  KexRaFire_PlayerTest(short nSprite, int nPitch); // env-only projectile probe
void KexBreakLoadSaveConstruct(void);
//
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
// Kex sector flag 0x20: a floor/ceiling may not support an actor merely
// because its radius overlaps the sector at an edge (lotag-83 map marker).
void KexNoStanding_Reset(void);
void KexNoStanding_Add(int nSprite);
int  KexNoStanding_Sector(int nSector);
int  KexNoStanding_Active(void);
// Directed Kex face flags that block nonplayers, or AI at a floor-height
// transition. The converter carries them in wall.extra until this reset.
void KexWall_Reset(void);
void KexWall_ClipBegin(int nSprite, unsigned int clipmask);
void KexWall_ClipEnd(void);
void KexWall_ViewBegin(int x, int y, int z);
void KexWall_ViewEnd(void);
// Open authored KEX Toggle faces touched by an A_RadialBlast whose third
// argument permits geometry destruction. Radius is in the remaster's units.
void KexToggle_Blast(int x, int y, int z, int nSector, int nRadius);
void KexHold_Reset(void);                // movers that hold where their tag put them (lotag 73)
void KexHold_Add(int nSprite);
int  KexHold_Sector(int nSector);
int  KexHold_BombFloor(int nSector, int *pZ);   // the EXPLODE sector's authored floor (lotag 74)
int  KexHold_PadTop(int nSector, int *pZ);      // legacy DROP_PAD top marker (lotag 81; pre-kexpad output)
void KexForce_Update(void);              // loads/counts the contact-controlled force fields
int  KexForce_OnWallHit(int nSprite, int nWall);  // kexPuppet::OnCollide force-field path
int  KexForce_StunMove(int nSprite);     // remaster LockTime while electrocuted
void KexForce_Tick(void);                 // the green ElectrocuteFlash fade
int  KexForce_ElectrocuteAlpha(void);
int  KexForce_IsElectrocuted(int nSprite);
void KexHud_DamageFlash(void);             // source red 192-alpha overlay
void KexHud_PickupFlash(void);             // source blue 192-alpha overlay
int  KexHud_DamageFlashAlpha(void);
int  KexHud_PickupFlashAlpha(void);
void KexHudFlash_Tick(void);
void KexScript_BossTick(void);          // the VB_KEXBOSS_TEST hook
void KexScript_BossEvent(int nSprite, char const *why); // authored boss death callback
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
