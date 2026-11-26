// NNBuild JavaScript scripting layer (QuickJS).
//
// Public C API used by the game to drive an embedded QuickJS runtime: load a
// user script via `-js file.js`, fire gameplay events into it, let it draw HUD
// text, spawn actors, exchange multiplayer chat, read/write files, etc.
//
// The implementation lives per-game (source/nnbuild/src/nnbjs.cpp for Duke);
// this header is the shared contract so other games can provide the same API.
#pragma once
#ifndef NNBJS_H_
#define NNBJS_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// CON struct kinds for the generic data accessors (nnb.getActor/getPlayer/...).
// These mirror the label tables CON itself uses, so every CON-exposed member is
// reachable by name.
enum {
    NNBJS_ACTOR = 0, NNBJS_PLAYER, NNBJS_INPUT, NNBJS_SECTOR, NNBJS_WALL,
    NNBJS_PROJECTILE, NNBJS_ACTIVEPROJ, NNBJS_TILEDATA, NNBJS_USERDEF
};

// -js handling / lifecycle.
int  NNBJS_IsEnabled(void);                 // a script was requested and the runtime is up
void NNBJS_CaptureLaunchDir(void);          // record the launch cwd early (before the game chdir's); holds autojs/
void NNBJS_SetExecutablePath(const char *path); // record argv[0] so generators can find bundled templates safely
int  NNBJS_GenerateProject(const char *name, int typescript); // create autojs/<name>/ JS or TS starter; 0 on success
void NNBJS_SetGenericMode(int enabled);     // run core JS/debugger APIs without Duke game-state bindings
void NNBJS_SetScriptPath(const char *path); // remember the -js argument (before init)
void NNBJS_SetDevtools(int enabled);         // enable the lightweight JavaScript diagnostics overlay
int  NNBJS_DevtoolsEnabled(void);
void NNBJS_SetDebug(int enabled);            // enable the localhost debugger attach endpoint
int  NNBJS_DebugEnabled(void);
void NNBJS_SetDebugPort(int port);
int  NNBJS_DebugPort(void);
const char *NNBJS_DevtoolsLog(int index);    // oldest-first recent console log, or NULL
int  NNBJS_DevtoolsPaused(void);
void NNBJS_DevtoolsResume(void);
const char *NNBJS_DevtoolsCurrentEvent(void);
int  NNBJS_DevtoolsEvaluate(const char *expression, char *out, int outSize);
void NNBJS_DevtoolsFocus(void);             // show/focus the external DevTools surface
void NNBJS_DevtoolsStartDebuggerBridge(void);
int  NNBJS_DevtoolsSourceCount(void);
const char *NNBJS_DevtoolsSourcePath(int index);
int  NNBJS_DevtoolsReadSource(int index, char *out, int outSize);
/* Thread-safe JSON transport used by the standalone DevTools surface. */
int  NNBJS_DevtoolsDebuggerSend(const char *json);
int  NNBJS_DevtoolsDebuggerReceive(char *out, int outSize);
int  NNBJS_DevtoolsDebuggerReceiveForClient(int client, char *out, int outSize);
void NNBJS_DevtoolsPump(void);
// Give the detached DevTools window first refusal of its own SDL input event.
// `event` is an SDL_Event pointer, intentionally opaque to game-independent users.
int  NNBJS_DevtoolsProcessEvent(const void *event);
// True while the detached DevTools, rather than the game, owns keyboard or
// mouse focus.  SDL's Cocoa mouse events may not always retain a window ID
// while relative mouse mode is being disabled.
int  NNBJS_DevtoolsOwnsInput(void);
// SDL relative mouse mode is process-wide on Cocoa.  While a detached
// DevTools window is visible, the game must not recapture it or macOS routes
// the DevTools activation click back to the game window.
int  NNBJS_DevtoolsSuppressGameMouseCapture(void);
void NNBJS_DevtoolsShutdownWindow(void);
void NNBJS_Init(void);                      // build the runtime and run the script (once, after game init)
void NNBJS_BeginLevelAttachments(void);
void NNBJS_EndLevelAttachments(void);
void NNBJS_ActivateAttachment(const char *script, const char *map, const char *kind, int index);
void NNBJS_ClearAttachments(void);
void NNBJS_NotifySpriteDeleted(int spriteNum);
void NNBJS_RunFixedUpdate(double deltaSeconds);
void NNBJS_Shutdown(void);                  // tear the runtime down
// Per-frame: fire "frame", update map components and pump JS jobs/timers. Pass
// nonzero while a level is being played (not paused or in a menu) so that map
// script components receive onUpdate.
#ifdef __cplusplus
void NNBJS_RunFrame(int activeLevel = 0);
#else
void NNBJS_RunFrame(int activeLevel);
#endif
void NNBJS_DispatchCommands(void);

// Event dispatch — call these from real game hook points. `event` is a short
// name the script listens for with nnb.on("name", fn).
void NNBJS_FireSimple(const char *event);                                     // e.g. "playerDied"
void NNBJS_FireMenu(const char *event, int menu, int previousMenu);
void NNBJS_FireActor(const char *event, int spriteNum, int owner, int tile);  // e.g. "actorSpawned"/"itemTaken"
void NNBJS_FireChat(const char *event, int playerNum, const char *text);  // e.g. "chatMessage"/"netMessage"
int NNBJS_RunServerCommand(int player, uint32_t connection, const char *text);

// Fire a CON event (EVENT_*) into JS. Called from the CON event dispatcher so every
// CON event is listenable as nnb.on("EVENT_NAME", e => ...). Cheap no-op when no
// script listens for that event number.
void NNBJS_FireConEvent(int eventNum, int spriteNum, int playerNum, int dist);

// Per-event-number flag: does a JS script listen for this CON event? The engine's
// VM_HaveEvent() ORs this in so the event gets dispatched even without a CON handler.
#define NNBJS_MAXCONEVENTS 1024
extern unsigned char g_nnbjsConEventListened[NNBJS_MAXCONEVENTS];

// HUD: draw all script-registered text. Call from the game's HUD/overlay pass.
void NNBJS_DrawHUD(void);

// Menu integration.  The JavaScript API can replace any string rendered by
// menutext() and select a tile used for the standard menu background.
const char *NNBJS_MenuText(const char *text);
int NNBJS_MenuBackground(void); // tile number, or -1 when not overridden
int NNBJS_MenuRoute(int fromMenu, int itemIndex, int requestedMenu);

// Script-registered HUD text, shared between the QuickJS side (which fills it)
// and the game side (which draws it). x/y are in 0..320 / 0..200 HUD space.
typedef struct { int used, id, x, y, pal, shade, z; char str[192]; } nnbjs_text_t;
nnbjs_text_t *NNBJS_TextSlots(int *count);   // returns the fixed slot array

#ifdef __cplusplus
}
#endif

#endif // NNBJS_H_
