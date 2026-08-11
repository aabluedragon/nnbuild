// VibeBuild32 JavaScript scripting layer (QuickJS).
//
// Public C API used by the game to drive an embedded QuickJS runtime: load a
// user script via `-js file.js`, fire gameplay events into it, let it draw HUD
// text, spawn actors, exchange multiplayer chat, read/write files, etc.
//
// The implementation lives per-game (source/vibebuild32/src/vbjs.cpp for Duke);
// this header is the shared contract so other games can provide the same API.
#pragma once
#ifndef VBJS_H_
#define VBJS_H_

#ifdef __cplusplus
extern "C" {
#endif

// CON struct kinds for the generic data accessors (vb.getActor/getPlayer/...).
// These mirror the label tables CON itself uses, so every CON-exposed member is
// reachable by name.
enum {
    VBJS_ACTOR = 0, VBJS_PLAYER, VBJS_INPUT, VBJS_SECTOR, VBJS_WALL,
    VBJS_PROJECTILE, VBJS_ACTIVEPROJ, VBJS_TILEDATA, VBJS_USERDEF
};

// -js handling / lifecycle.
int  VBJS_IsEnabled(void);                 // a script was requested and the runtime is up
void VBJS_SetScriptPath(const char *path); // remember the -js argument (before init)
void VBJS_Init(void);                      // build the runtime and run the script (once, after game init)
void VBJS_Shutdown(void);                  // tear the runtime down
void VBJS_RunFrame(void);                  // per-frame: fire "frame", pump pending JS jobs/timers

// Event dispatch — call these from real game hook points. `event` is a short
// name the script listens for with vb.on("name", fn).
void VBJS_FireSimple(const char *event);                                     // e.g. "playerDied"
void VBJS_FireActor(const char *event, int spriteNum, int owner, int tile);  // e.g. "actorSpawned"/"itemTaken"
void VBJS_FireChat(const char *event, int playerNum, const char *text);  // e.g. "chatMessage"/"netMessage"

// HUD: draw all script-registered text. Call from the game's HUD/overlay pass.
void VBJS_DrawHUD(void);

// Script-registered HUD text, shared between the QuickJS side (which fills it)
// and the game side (which draws it). x/y are in 0..320 / 0..200 HUD space.
typedef struct { int used, id, x, y, pal, shade, z; char str[192]; } vbjs_text_t;
vbjs_text_t *VBJS_TextSlots(int *count);   // returns the fixed slot array

#ifdef __cplusplus
}
#endif

#endif // VBJS_H_
