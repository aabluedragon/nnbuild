#ifndef global_h_
#define global_h_
#include "build.h"
#include "compat.h"
#include "duke3d.h"
#include "mmulti.h"
#include "sounds.h"

#ifdef global_c_
#define G_EXTERN
#else
#define G_EXTERN extern
#endif

#define MAXINTERPOLATIONS MAXSPRITES
G_EXTERN int32_t g_interpolationLock;
G_EXTERN int g_interpolationCnt;
G_EXTERN int oldipos[MAXINTERPOLATIONS];
G_EXTERN int bakipos[MAXINTERPOLATIONS];
G_EXTERN int *curipos[MAXINTERPOLATIONS];

// Pre-initialized
#ifndef global_c_
extern int g_spriteGravity;
extern int g_actorRespawnTime;
extern int g_itemRespawnTime;
extern int g_timerTicsPerSecond;
extern int g_scriptSize;
extern int g_playerFriction;
extern int g_numFreezeBounces;
extern short g_spriteDeleteQueueSize;
extern char g_numVolumes;
extern char g_numGametypes;

extern char EpisodeNames[MAXVOLUMES][33];
extern char SkillNames[5][33];
extern char GametypeNames[MAXGAMETYPES][33];
extern int GametypeFlags[MAXGAMETYPES];
extern short BlimpSpawnSprites[15];
extern char CheatKeys[2];
extern char setupfilename[BMAX_PATH];

// Multiplayer
extern int g_networkBroadcastMode; // 0: master/slave, 1: peer-to-peer
#endif

#ifdef __cplusplus
extern "C" {
#endif
G_EXTERN short g_globalRandom;
G_EXTERN short neartagsector, neartagwall, neartagsprite;
G_EXTERN short SpriteDeletionQueue[1024], g_spriteDeleteQueuePos;

G_EXTERN int ototalclock, lockclock;
G_EXTERN int neartaghitdist;
G_EXTERN int g_startArmorAmount;
G_EXTERN int g_musicSize;
G_EXTERN int g_musicIndex;
G_EXTERN int g_currentMenu;
G_EXTERN int g_currentFrameRate;
G_EXTERN int g_impactDamage;
G_EXTERN int g_scriptDebug;
G_EXTERN int g_groupFileHandle;
G_EXTERN int g_showShareware;
G_EXTERN int g_damageCameras;
G_EXTERN int g_freezerSelfDamage;
G_EXTERN int g_tripbombLaserMode;
G_EXTERN int g_rpgBlastRadius;
G_EXTERN int g_pipebombBlastRadius;
G_EXTERN int g_tripbombBlastRadius;
G_EXTERN int g_shrinkerBlastRadius;
G_EXTERN int g_morterBlastRadius;
G_EXTERN int g_bouncemineBlastRadius;
G_EXTERN int g_seenineBlastRadius;
G_EXTERN int g_gameQuit;
G_EXTERN int g_doQuickSave;
G_EXTERN int everyothertime;
G_EXTERN int fricxv, fricyv;
G_EXTERN int totalmemory;
G_EXTERN unsigned int g_moveThingsCount;
G_EXTERN char g_loadFromGroupOnly;
G_EXTERN char g_earthquakeTime;
G_EXTERN char g_RTSPlaying;
G_EXTERN char pus, pub;

G_EXTERN vec2_t g_origins[2048];

G_EXTERN animwalltype animwall[MAXANIMWALLS];
G_EXTERN short g_numAnimWalls;

G_EXTERN int *animateptr[MAXANIMATES];
G_EXTERN int animategoal[MAXANIMATES], animatevel[MAXANIMATES], g_animateCount;
G_EXTERN short animatesect[MAXANIMATES];

G_EXTERN int startofdynamicinterpolations;

G_EXTERN short camsprite;
G_EXTERN short g_mirrorWall[64], g_mirrorSector[64], g_mirrorCount;

G_EXTERN char buf[1024], tempbuf[2048], packbuf[576], menutextbuf[128];
G_EXTERN char typebuflen, typebuf[141];

G_EXTERN char *ScriptQuotes[MAXQUOTES], *ScriptQuoteRedefinitions[MAXQUOTES];

G_EXTERN short cyclers[MAXCYCLERS][6], g_numCyclers;

G_EXTERN intptr_t *g_scriptPtr, *insptr, *labelcode, *labeltype;
G_EXTERN intptr_t *actorscrptr[MAXTILES], *g_parsingActorPtr;
G_EXTERN intptr_t *script;
G_EXTERN int g_numLabels, g_numDefaultLabels;
G_EXTERN char *label;

G_EXTERN char EnvMusicFilename[MAXVOLUMES + 1][BMAX_PATH];
G_EXTERN char ActorType[MAXTILES];
G_EXTERN int SpriteFlags[MAXTILES];

// Sound
G_EXTERN sound_t g_sounds[MAXSOUNDS];
G_EXTERN char g_soundlocks[MAXSOUNDS];

// Clouds
G_EXTERN short g_numClouds, clouds[128], cloudx[128], cloudy[128];
G_EXTERN int cloudtotalclock;

// Structs
G_EXTERN map_t MapInfo[(MAXVOLUMES + 1) * MAXLEVELS]; // +1 volume for "intro", "briefing" music
G_EXTERN ActorData_t ActorExtra[MAXSPRITES];
G_EXTERN playerdata_t g_player[MAXPLAYERS];
G_EXTERN PlayerSpawn_t g_playerSpawnPoints[MAXSPAWNPOINTS];
G_EXTERN user_defs ud;
G_EXTERN projectile_t ProjectileData[MAXTILES], DefaultProjectileData[MAXTILES];
G_EXTERN STATUSBARTYPE sbar;
G_EXTERN spriteinterpolate sprpos[MAXSPRITES];

// Multiplayer/Demos
G_EXTERN int playerswhenstarted;
G_EXTERN int screenpeek;
G_EXTERN int vel, svel;
G_EXTERN char szPlayerName[32];
G_EXTERN char ready2send;
G_EXTERN char g_numPlayerSprites;
G_EXTERN signed char multiwho, multipos, multiwhat, multiflag;
G_EXTERN fix16_t q16horiz;
G_EXTERN fix16_t q16angvel;
G_EXTERN input_t loc;
G_EXTERN input_t recsync[RECSYNCBUFSIZ];
G_EXTERN input_t inputfifo[MOVEFIFOSIZ][MAXPLAYERS];
#ifdef __cplusplus
}
#endif

#endif