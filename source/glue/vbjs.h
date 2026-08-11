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
void VBJS_CaptureLaunchDir(void);          // record the launch cwd early (before the game chdir's); holds autojs/
void VBJS_SetGenericMode(int enabled);     // run core JS/debugger APIs without Duke game-state bindings
void VBJS_SetScriptPath(const char *path); // remember the -js argument (before init)
int  VBJS_GenerateProject(const char *name, int typescript); // create autojs/<name>/ JS or TS starter; 0 on success
void VBJS_SetDevtools(int enabled);         // enable the lightweight JavaScript diagnostics overlay
int  VBJS_DevtoolsEnabled(void);
void VBJS_SetDebug(int enabled);            // enable the localhost debugger attach endpoint
int  VBJS_DebugEnabled(void);
void VBJS_SetDebugPort(int port);
int  VBJS_DebugPort(void);
const char *VBJS_DevtoolsLog(int index);    // oldest-first recent console log, or NULL
int  VBJS_DevtoolsPaused(void);
void VBJS_DevtoolsResume(void);
const char *VBJS_DevtoolsCurrentEvent(void);
int  VBJS_DevtoolsEvaluate(const char *expression, char *out, int outSize);
void VBJS_DevtoolsFocus(void);             // show/focus the external DevTools surface
void VBJS_DevtoolsStartDebuggerBridge(void);
int  VBJS_DevtoolsSourceCount(void);
const char *VBJS_DevtoolsSourcePath(int index);
int  VBJS_DevtoolsReadSource(int index, char *out, int outSize);
/* Thread-safe JSON transport used by the standalone DevTools surface. */
int  VBJS_DevtoolsDebuggerSend(const char *json);
int  VBJS_DevtoolsDebuggerReceive(char *out, int outSize);
int  VBJS_DevtoolsDebuggerReceiveForClient(int client, char *out, int outSize);
void VBJS_DevtoolsPump(void);
// Give the detached DevTools window first refusal of its own SDL input event.
// `event` is an SDL_Event pointer, intentionally opaque to game-independent users.
int  VBJS_DevtoolsProcessEvent(const void *event);
// True while the detached DevTools, rather than the game, owns keyboard or
// mouse focus.  SDL's Cocoa mouse events may not always retain a window ID
// while relative mouse mode is being disabled.
int  VBJS_DevtoolsOwnsInput(void);
// SDL relative mouse mode is process-wide on Cocoa.  While a detached
// DevTools window is visible, the game must not recapture it or macOS routes
// the DevTools activation click back to the game window.
int  VBJS_DevtoolsSuppressGameMouseCapture(void);
void VBJS_DevtoolsShutdownWindow(void);
void VBJS_Init(void);                      // build the runtime and run the script (once, after game init)
void VBJS_Shutdown(void);                  // tear the runtime down
void VBJS_RunFrame(void);                  // per-frame: fire "frame", pump pending JS jobs/timers

// Event dispatch — call these from real game hook points. `event` is a short
// name the script listens for with vb.on("name", fn).
void VBJS_FireSimple(const char *event);                                     // e.g. "playerDied"
void VBJS_FireMenu(const char *event, int menu, int previousMenu);
void VBJS_FireActor(const char *event, int spriteNum, int owner, int tile);  // e.g. "actorSpawned"/"itemTaken"
void VBJS_FireChat(const char *event, int playerNum, const char *text);  // e.g. "chatMessage"/"netMessage"

// Fire a CON event (EVENT_*) into JS. Called from the CON event dispatcher so every
// CON event is listenable as vb.on("EVENT_NAME", e => ...). Cheap no-op when no
// script listens for that event number.
void VBJS_FireConEvent(int eventNum, int spriteNum, int playerNum, int dist);

// Per-event-number flag: does a JS script listen for this CON event? The engine's
// VM_HaveEvent() ORs this in so the event gets dispatched even without a CON handler.
#define VBJS_MAXCONEVENTS 1024
extern unsigned char g_vbjsConEventListened[VBJS_MAXCONEVENTS];

// HUD: draw all script-registered text. Call from the game's HUD/overlay pass.
void VBJS_DrawHUD(void);

// Menu integration.  The JavaScript API can replace any string rendered by
// menutext() and select a tile used for the standard menu background.
const char *VBJS_MenuText(const char *text);
int VBJS_MenuBackground(void); // tile number, or -1 when not overridden
int VBJS_MenuRoute(int fromMenu, int itemIndex, int requestedMenu);

// Script-registered HUD text, shared between the QuickJS side (which fills it)
// and the game side (which draws it). x/y are in 0..320 / 0..200 HUD space.
typedef struct { int used, id, x, y, pal, shade, z; char str[192]; } vbjs_text_t;
vbjs_text_t *VBJS_TextSlots(int *count);   // returns the fixed slot array

#ifdef __cplusplus
}
#endif

#endif // VBJS_H_
