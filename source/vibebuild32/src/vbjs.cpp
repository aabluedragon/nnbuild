// VibeBuild32 JavaScript scripting layer (QuickJS-ng) — Duke3D implementation.
//
// Provides an embedded JS runtime driven by `-js file.js`:
//   * ES modules (import/export), a CommonJS-style require(), and an `fs` module
//   * a `vb` module/global: event listeners (vb.on), HUD text, actor spawning,
//     multiplayer chat, CON gamevar access
//   * the game fires gameplay events into JS via the VBJS_Fire* C API.
//
// Game-specific bindings (spawn, gamevars, chat, HUD draw) are marked "Duke
// binding" and are the seam to port this to the other games later.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include <deque>

extern "C" {
#include "quickjs.h"
#include "quickjs-debugger.h"
}
#include "vbjs.h"

// ---- Duke game hooks (implemented at the bottom, kept together for porting) ----
namespace vbjs_game {
    void log(const char *s);                                  // console output into the game log
    int  spawn(int tile, int x, int y, int z, int owner);     // -> spriteNum, or -1
    void sendChat(const char *text);                          // broadcast a multiplayer/console message
    int  getGamevar(const char *name, int spriteNum, int playerNum, int *ok);
    void setGamevar(const char *name, int value, int spriteNum, int playerNum);
    int  playerSprite(void);                                  // current view player's sprite, or -1
    void spriteXYZ(int spriteNum, int *x, int *y, int *z, int *ok);
    void runCommand(const char *cmd);                         // execute an OSD console/cvar command
    // Generic CON struct access (kind = VBJS_ACTOR/PLAYER/... from vbjs.h).
    int  conGet(int kind, int index, const char *member, int parm2, int *ok);
    void conSet(int kind, int index, const char *member, int value, int parm2);
    int  conDefine(const char *name, int *ok);                // resolve a CON define/constant by name
    // CON action commands.
    void sound(int num, int sprite);
    int  shoot(int sprite, int tile);
    void addAmmo(int playerNum, int weapon, int amount);
    void addWeapon(int playerNum, int weapon, int switchTo);
    int  A_Spawn(int spriteNum, int tileNum);
    int  A_InsertSprite(int whatsect, int s_x, int s_y, int s_z, int s_pn, int s_s,
                        int s_xr, int s_yr, int s_a, int s_ve, int s_zv, int s_ow, int s_ss);
    // CON event (EVENT_*) name<->number, for listening to all CON events.
    int         eventNum(const char *name);   // "EVENT_SPAWN" -> id, or -1
    const char *eventName(int num);           // id -> "EVENT_SPAWN", or null
}

// Which CON event numbers have a JS listener. Exposed as a plain global so the
// engine's VM_HaveEvent() can OR it in inline (see gameexec.h): a CON event is
// dispatched when a CON handler OR a JS listener wants it.
extern "C" { unsigned char g_vbjsConEventListened[VBJS_MAXCONEVENTS]; }
#define s_conEventListened g_vbjsConEventListened

// ------------------------------------------------------------------------------

static JSRuntime *s_rt  = nullptr;
static JSContext *s_ctx = nullptr;
static char  s_scriptDir[1024]  = {0};   // dir of the script currently being loaded (require base)
static int   s_enabled = 0;
static int   s_initTried = 0;            // guards a single init attempt
static int   s_startPending = 0;
static int   s_startWaitFrames = 0;
static int   s_scriptsLoaded = 0;
static int   s_devtools = 0;
enum { VBJS_DEVTOOLS_LOGS = 12 };
static char s_devtoolsLogs[VBJS_DEVTOOLS_LOGS][192];
static int s_devtoolsLogCount = 0;
static int s_devtoolsPaused = 0;
static char s_devtoolsCurrentEvent[64];
static JSValue s_devtoolsEvent = JS_UNDEFINED;

/* The engine pauses on the game thread, while the DevTools HTTP listener runs
 * independently.  This transport is the narrow, synchronized bridge between
 * them; no QuickJS object is ever touched from the listener thread. */
struct vbjs_debug_transport_t
{
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
    std::deque<char> inbound, outbound[2]; // browser inspector, VS Code DAP bridge
    int closed = 0;
};
static vbjs_debug_transport_t s_debugTransport;

static size_t vbjs_debug_read(void *opaque, char *buffer, size_t length)
{
    vbjs_debug_transport_t *transport = (vbjs_debug_transport_t *)opaque;
    pthread_mutex_lock(&transport->mutex);
    while (transport->inbound.empty() && !transport->closed) pthread_cond_wait(&transport->changed, &transport->mutex);
    size_t n = 0; while (n < length && !transport->inbound.empty()) { buffer[n++] = transport->inbound.front(); transport->inbound.pop_front(); }
    pthread_mutex_unlock(&transport->mutex); return n;
}
static size_t vbjs_debug_write(void *opaque, const char *buffer, size_t length)
{
    vbjs_debug_transport_t *transport = (vbjs_debug_transport_t *)opaque;
    pthread_mutex_lock(&transport->mutex); for (int client = 0; client < 2; ++client) for (size_t i = 0; i < length; ++i) transport->outbound[client].push_back(buffer[i]); pthread_cond_broadcast(&transport->changed); pthread_mutex_unlock(&transport->mutex); return length;
}
static size_t vbjs_debug_peek(void *opaque)
{
    vbjs_debug_transport_t *transport = (vbjs_debug_transport_t *)opaque;
    pthread_mutex_lock(&transport->mutex); size_t n = transport->inbound.size(); pthread_mutex_unlock(&transport->mutex); return n;
}
static void vbjs_debug_close(JSRuntime *, void *opaque)
{
    vbjs_debug_transport_t *transport = (vbjs_debug_transport_t *)opaque;
    pthread_mutex_lock(&transport->mutex); transport->closed = 1; pthread_cond_broadcast(&transport->changed); pthread_mutex_unlock(&transport->mutex);
}

static void vbjs_devtools_log(const char *line)
{
    if (!s_devtools) return;
    snprintf(s_devtoolsLogs[s_devtoolsLogCount % VBJS_DEVTOOLS_LOGS], sizeof(s_devtoolsLogs[0]), "%s", line ? line : "");
    ++s_devtoolsLogCount;
}

// The set of scripts to load: -js files plus autojs/<name>/index.{ts,js}.
struct vbjs_script_t { char path[1024]; };
enum { VBJS_MAX_SCRIPTS = 64 };
static vbjs_script_t s_scripts[VBJS_MAX_SCRIPTS];
static int s_scriptCount = 0;
static char s_launchDir[1024];   // captured at app_main() before any chdir; holds autojs/

static int vbjs_write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int failed = fputs(text, f) < 0 || fclose(f) != 0;
    return failed ? -1 : 0;
}

static int vbjs_safe_project_name(const char *in, char *out, size_t outsz)
{
    if (!in || !in[0]) return 0;
    size_t n = 0;
    for (; *in; ++in)
    {
        unsigned char c = (unsigned char)*in;
        if (!(isalnum(c) || c == '-' || c == '_')) return -1;
        if (n + 1 >= outsz) return -1;
        out[n++] = (char)c;
    }
    out[n] = 0;
    return n ? 0 : -1;
}

static void vbjs_add_script(const char *path)
{
    if (!path || s_scriptCount >= VBJS_MAX_SCRIPTS) return;
    for (int i = 0; i < s_scriptCount; i++)
        if (!strcmp(s_scripts[i].path, path)) return;   // dedupe
    snprintf(s_scripts[s_scriptCount++].path, sizeof(s_scripts[0].path), "%s", path);
}

static JSValue s_events;   // { eventName: [fn, ...] }

// Script-registered HUD text, kept C-side so the draw hook is trivial.
// (vbjs_text_t is declared in vbjs.h so the game-side draw code shares the layout.)
enum { VBJS_MAX_TEXT = 64 };
static vbjs_text_t s_text[VBJS_MAX_TEXT];

// ---------------------------------------------------------------- utilities ---

static char *vbjs_read_file(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    if (n < 0) { fclose(f); return nullptr; }
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return nullptr; }
    size_t rd = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[rd] = 0;
    if (out_len) *out_len = rd;
    return buf;
}

// TypeScript -> JavaScript (implemented in vbjs_ts.cpp; strips type syntax so .ts runs).
extern "C" char *vbjs_ts_to_js(const char *src, size_t *len);

// Read a source file; transpile it from TypeScript if it ends in .ts / .mts / .cts.
static char *vbjs_load_source(const char *path, size_t *out_len)
{
    size_t len = 0; char *src = vbjs_read_file(path, &len);
    if (!src) return nullptr;
    size_t n = strlen(path);
    int isTs = (n >= 3 && (!strcasecmp(path + n - 3, ".ts") ||
               (n >= 4 && (!strcasecmp(path + n - 4, ".mts") || !strcasecmp(path + n - 4, ".cts")))));
    if (isTs)
    {
        size_t jslen = 0;
        char *js = vbjs_ts_to_js(src, &jslen);
        free(src);
        if (out_len) *out_len = jslen;
        return js;
    }
    if (out_len) *out_len = len;
    return src;
}

// Resolve a (possibly relative) module/require path against the main script dir.
static void vbjs_resolve(const char *name, char *out, size_t outsz)
{
    if (name[0] == '/' || s_scriptDir[0] == 0)
        snprintf(out, outsz, "%s", name);
    else
        snprintf(out, outsz, "%s/%s", s_scriptDir, name);
}

static void vbjs_dump_error(JSContext *ctx)
{
    JSValue exc = JS_GetException(ctx);
    const char *msg = JS_ToCString(ctx, exc);
    vbjs_game::log(msg ? msg : "(unknown JS error)");
    vbjs_devtools_log(msg ? msg : "(unknown JS error)");
    if (msg) JS_FreeCString(ctx, msg);
    JSValue stack = JS_GetPropertyStr(ctx, exc, "stack");
    if (!JS_IsUndefined(stack))
    {
        const char *st = JS_ToCString(ctx, stack);
        if (st) { vbjs_game::log(st); vbjs_devtools_log(st); JS_FreeCString(ctx, st); }
    }
    JS_FreeValue(ctx, stack);
    JS_FreeValue(ctx, exc);
}

// ------------------------------------------------------------------ console ---

static JSValue js_console_log(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    char line[1024]; line[0] = 0; size_t off = 0;
    for (int i = 0; i < argc; i++)
    {
        const char *s = JS_ToCString(ctx, argv[i]);
        if (s)
        {
            off += (size_t)snprintf(line + off, sizeof(line) - off, "%s%s", i ? " " : "", s);
            JS_FreeCString(ctx, s);
        }
        if (off >= sizeof(line) - 1) break;
    }
    vbjs_game::log(line);
    vbjs_devtools_log(line);
    return JS_UNDEFINED;
}

// ----------------------------------------------------------------- fs module ---

static JSValue js_fs_readFile(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    if (argc < 1) return JS_ThrowTypeError(ctx, "fs.readFile(path)");
    const char *path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;
    size_t len = 0; char *buf = vbjs_read_file(path, &len);
    JS_FreeCString(ctx, path);
    if (!buf) return JS_ThrowReferenceError(ctx, "fs.readFile: cannot open file");
    JSValue v = JS_NewStringLen(ctx, buf, len);
    free(buf);
    return v;
}

static JSValue js_fs_writeImpl(JSContext *ctx, int argc, JSValueConst *argv, const char *mode)
{
    if (argc < 2) return JS_ThrowTypeError(ctx, "fs.writeFile(path, data)");
    const char *path = JS_ToCString(ctx, argv[0]);
    size_t len = 0; const char *data = JS_ToCStringLen(ctx, &len, argv[1]);
    if (!path || !data) { if (path) JS_FreeCString(ctx, path); if (data) JS_FreeCString(ctx, data); return JS_EXCEPTION; }
    FILE *f = fopen(path, mode);
    int ok = 0;
    if (f) { ok = (fwrite(data, 1, len, f) == len); fclose(f); }
    JS_FreeCString(ctx, path); JS_FreeCString(ctx, data);
    if (!ok) return JS_ThrowReferenceError(ctx, "fs.writeFile: cannot write file");
    return JS_UNDEFINED;
}
static JSValue js_fs_writeFile (JSContext *ctx, JSValueConst, int argc, JSValueConst *argv) { return js_fs_writeImpl(ctx, argc, argv, "wb"); }
static JSValue js_fs_appendFile(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv) { return js_fs_writeImpl(ctx, argc, argv, "ab"); }

static JSValue js_fs_exists(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    if (argc < 1) return JS_FALSE;
    const char *path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_FALSE;
    FILE *f = fopen(path, "rb");
    int ex = (f != nullptr);
    if (f) fclose(f);
    JS_FreeCString(ctx, path);
    return JS_NewBool(ctx, ex);
}

// ----------------------------------------------------------------- vb: events ---

static JSValue js_vb_on(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    if (argc < 2 || !JS_IsFunction(ctx, argv[1]))
        return JS_ThrowTypeError(ctx, "vb.on(event, handler)");
    const char *ev = JS_ToCString(ctx, argv[0]);
    if (!ev) return JS_EXCEPTION;
    // The public API accepts both `STRAFELEFT` and `EVENT_STRAFELEFT`, but
    // engine dispatch always uses the canonical EVENT_* spelling. Store under
    // that spelling so the callback (and a source breakpoint inside it) is
    // actually reachable.
    int const conEv = vbjs_game::eventNum(ev);
    const char *eventKey = conEv >= 0 ? vbjs_game::eventName(conEv) : ev;
    JSValue arr = JS_GetPropertyStr(ctx, s_events, eventKey);
    if (!JS_IsArray(arr))
    {
        JS_FreeValue(ctx, arr);
        arr = JS_NewArray(ctx);
        JS_SetPropertyStr(ctx, s_events, eventKey, JS_DupValue(ctx, arr));
    }
    JSValue lenv = JS_GetPropertyStr(ctx, arr, "length");
    uint32_t len = 0; JS_ToUint32(ctx, &len, lenv); JS_FreeValue(ctx, lenv);
    JS_SetPropertyUint32(ctx, arr, len, JS_DupValue(ctx, argv[1]));
    JS_FreeValue(ctx, arr);
    // If this is a CON event name, mark it so the CON dispatch hook fires it.
    if (conEv >= 0 && conEv < VBJS_MAXCONEVENTS) s_conEventListened[conEv] = 1;
    JS_FreeCString(ctx, ev);
    return JS_UNDEFINED;
}

static JSValue js_vb_off(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    if (argc < 1) return JS_UNDEFINED;
    const char *ev = JS_ToCString(ctx, argv[0]);
    if (ev)
    {
        int const conEv = vbjs_game::eventNum(ev);
        const char *eventKey = conEv >= 0 ? vbjs_game::eventName(conEv) : ev;
        JS_SetPropertyStr(ctx, s_events, eventKey, JS_NewArray(ctx));
        JS_FreeCString(ctx, ev);
    }
    return JS_UNDEFINED;
}

// Fire an event to every registered handler with the given args.
static void vbjs_fire(const char *event, int argc, JSValueConst *argv)
{
    if (!s_ctx || s_devtoolsPaused) return;
    JSContext *ctx = s_ctx;
    JSValue arr = JS_GetPropertyStr(ctx, s_events, event);
    if (JS_IsArray(arr))
    {
        JSValue lenv = JS_GetPropertyStr(ctx, arr, "length");
        uint32_t len = 0; JS_ToUint32(ctx, &len, lenv); JS_FreeValue(ctx, lenv);
        for (uint32_t i = 0; i < len; i++)
        {
            JSValue fn = JS_GetPropertyUint32(ctx, arr, i);
            JSValue r  = JS_Call(ctx, fn, JS_UNDEFINED, argc, (JSValueConst *)argv);
            if (JS_IsException(r)) vbjs_dump_error(ctx);
            JS_FreeValue(ctx, r);
            JS_FreeValue(ctx, fn);
        }
    }
    JS_FreeValue(ctx, arr);
}

// ----------------------------------------------------------------- vb: HUD ---

static JSValue js_vb_drawText(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    if (argc < 4) return JS_ThrowTypeError(ctx, "vb.drawText(id, x, y, text[, pal[, shade]])");
    int32_t id = 0, x = 0, y = 0, pal = 0, shade = 0;
    JS_ToInt32(ctx, &id, argv[0]); JS_ToInt32(ctx, &x, argv[1]); JS_ToInt32(ctx, &y, argv[2]);
    if (argc > 4) JS_ToInt32(ctx, &pal, argv[4]);
    if (argc > 5) JS_ToInt32(ctx, &shade, argv[5]);
    const char *str = JS_ToCString(ctx, argv[3]);
    if (!str) return JS_EXCEPTION;

    vbjs_text_t *slot = nullptr, *freeSlot = nullptr;
    for (auto &t : s_text)
    {
        if (t.used && t.id == id) { slot = &t; break; }
        if (!t.used && !freeSlot) freeSlot = &t;
    }
    if (!slot) slot = freeSlot;
    if (slot)
    {
        slot->used = 1; slot->id = id; slot->x = x; slot->y = y; slot->pal = pal; slot->shade = shade;
        snprintf(slot->str, sizeof(slot->str), "%s", str);
    }
    JS_FreeCString(ctx, str);
    return JS_UNDEFINED;
}

static JSValue js_vb_clearText(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    int32_t id = 0; int all = (argc < 1);
    if (!all) JS_ToInt32(ctx, &id, argv[0]);
    for (auto &t : s_text) if (all || (t.used && t.id == id)) t.used = 0;
    return JS_UNDEFINED;
}

// Explicit source-level breakpoint. This is intentionally cooperative: QuickJS-ng
// has no debugger protocol, so the game pauses further JS dispatch at a safe frame
// boundary and the ImGui inspector can examine globals/current event and resume it.
static JSValue js_vb_breakpoint(JSContext *ctx, JSValueConst, int, JSValueConst *)
{
    if (s_devtools)
    {
        snprintf(s_devtoolsCurrentEvent, sizeof(s_devtoolsCurrentEvent), "vb.breakpoint()");
        JS_FreeValue(ctx, s_devtoolsEvent);
        s_devtoolsEvent = JS_UNDEFINED;
        s_devtoolsPaused = 1;
    }
    return JS_UNDEFINED;
}

// -------------------------------------------------- vb: gameplay bindings ---

static JSValue js_vb_spawn(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    int32_t tile = 0, x = 0, y = 0, z = 0, owner = -1;
    if (argc > 0) JS_ToInt32(ctx, &tile, argv[0]);
    if (argc > 1) JS_ToInt32(ctx, &x, argv[1]);
    if (argc > 2) JS_ToInt32(ctx, &y, argv[2]);
    if (argc > 3) JS_ToInt32(ctx, &z, argv[3]);
    if (argc > 4) JS_ToInt32(ctx, &owner, argv[4]);
    return JS_NewInt32(ctx, vbjs_game::spawn(tile, x, y, z, owner));
}

// Exact Duke actor APIs. Keep the native names and parameter order so scripts
// can use the same calls as engine code when they need low-level control.
static JSValue js_vb_A_Spawn(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    if (argc < 2) return JS_ThrowTypeError(ctx, "A_Spawn(spriteNum, tileNum)");
    int32_t spriteNum = 0, tileNum = 0;
    JS_ToInt32(ctx, &spriteNum, argv[0]); JS_ToInt32(ctx, &tileNum, argv[1]);
    return JS_NewInt32(ctx, vbjs_game::A_Spawn(spriteNum, tileNum));
}

static JSValue js_vb_A_InsertSprite(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    if (argc < 13) return JS_ThrowTypeError(ctx, "A_InsertSprite(whatsect, s_x, s_y, s_z, s_pn, s_s, s_xr, s_yr, s_a, s_ve, s_zv, s_ow, s_ss)");
    int32_t values[13] = {};
    for (int i = 0; i < 13; ++i) JS_ToInt32(ctx, &values[i], argv[i]);
    return JS_NewInt32(ctx, vbjs_game::A_InsertSprite(values[0], values[1], values[2], values[3], values[4], values[5], values[6],
                                                        values[7], values[8], values[9], values[10], values[11], values[12]));
}

static JSValue js_vb_sendMessage(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    if (argc < 1) return JS_UNDEFINED;
    const char *s = JS_ToCString(ctx, argv[0]);
    if (s) { vbjs_game::sendChat(s); JS_FreeCString(ctx, s); }
    return JS_UNDEFINED;
}

static JSValue js_vb_getGamevar(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    if (argc < 1) return JS_ThrowTypeError(ctx, "vb.getGamevar(name[, spriteNum[, playerNum]])");
    const char *name = JS_ToCString(ctx, argv[0]);
    if (!name) return JS_EXCEPTION;
    int32_t sp = -1, pl = -1;
    if (argc > 1) JS_ToInt32(ctx, &sp, argv[1]);
    if (argc > 2) JS_ToInt32(ctx, &pl, argv[2]);
    int ok = 0; int v = vbjs_game::getGamevar(name, sp, pl, &ok);
    JS_FreeCString(ctx, name);
    return ok ? JS_NewInt32(ctx, v) : JS_UNDEFINED;
}

static JSValue js_vb_setGamevar(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    if (argc < 2) return JS_ThrowTypeError(ctx, "vb.setGamevar(name, value[, spriteNum[, playerNum]])");
    const char *name = JS_ToCString(ctx, argv[0]);
    if (!name) return JS_EXCEPTION;
    int32_t val = 0, sp = -1, pl = -1;
    JS_ToInt32(ctx, &val, argv[1]);
    if (argc > 2) JS_ToInt32(ctx, &sp, argv[2]);
    if (argc > 3) JS_ToInt32(ctx, &pl, argv[3]);
    vbjs_game::setGamevar(name, val, sp, pl);
    JS_FreeCString(ctx, name);
    return JS_UNDEFINED;
}

// cvar.run("map E1L3") — execute any OSD console command / cvar assignment.
static JSValue js_cvar_run(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    if (argc < 1) return JS_ThrowTypeError(ctx, "cvar.run(command)");
    const char *cmd = JS_ToCString(ctx, argv[0]);
    if (!cmd) return JS_EXCEPTION;
    vbjs_game::runCommand(cmd);
    JS_FreeCString(ctx, cmd);
    return JS_UNDEFINED;
}

// cvar.set("r_maxfps", 60) — convenience: run "name value".
static JSValue js_cvar_set(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    if (argc < 2) return JS_ThrowTypeError(ctx, "cvar.set(name, value)");
    const char *name = JS_ToCString(ctx, argv[0]);
    const char *val  = JS_ToCString(ctx, argv[1]);
    if (name && val) { char buf[512]; snprintf(buf, sizeof(buf), "%s %s", name, val); vbjs_game::runCommand(buf); }
    if (name) JS_FreeCString(ctx, name);
    if (val)  JS_FreeCString(ctx, val);
    return JS_UNDEFINED;
}

// Generic CON struct getters/setters. `magic` is the struct kind (VBJS_ACTOR, ...).
// Args: index-based kinds -> (index, member[, parm2]); userdef -> (member[, parm2]).
static JSValue js_con_get(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv, int magic)
{
    int32_t index = 0, parm2 = 0; int ai = 0;
    if (magic != VBJS_USERDEF) { if (argc < 2) return JS_ThrowTypeError(ctx, "getX(index, member)"); JS_ToInt32(ctx, &index, argv[0]); ai = 1; }
    else if (argc < 1) return JS_ThrowTypeError(ctx, "getUserdef(member)");
    const char *member = JS_ToCString(ctx, argv[ai]);
    if (!member) return JS_EXCEPTION;
    if (argc > ai + 1) JS_ToInt32(ctx, &parm2, argv[ai + 1]);
    int ok = 0; int v = vbjs_game::conGet(magic, index, member, parm2, &ok);
    JS_FreeCString(ctx, member);
    return ok ? JS_NewInt32(ctx, v) : JS_UNDEFINED;
}

static JSValue js_con_set(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv, int magic)
{
    int32_t index = 0, value = 0, parm2 = 0; int ai = 0;
    if (magic != VBJS_USERDEF) { if (argc < 3) return JS_ThrowTypeError(ctx, "setX(index, member, value)"); JS_ToInt32(ctx, &index, argv[0]); ai = 1; }
    else if (argc < 2) return JS_ThrowTypeError(ctx, "setUserdef(member, value)");
    const char *member = JS_ToCString(ctx, argv[ai]);
    if (!member) return JS_EXCEPTION;
    JS_ToInt32(ctx, &value, argv[ai + 1]);
    if (argc > ai + 2) JS_ToInt32(ctx, &parm2, argv[ai + 2]);
    vbjs_game::conSet(magic, index, member, value, parm2);
    JS_FreeCString(ctx, member);
    return JS_UNDEFINED;
}

static JSValue js_vb_sound(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    int32_t num = 0, sprite = -1;
    if (argc > 0) JS_ToInt32(ctx, &num, argv[0]);
    if (argc > 1) JS_ToInt32(ctx, &sprite, argv[1]);
    vbjs_game::sound(num, sprite);
    return JS_UNDEFINED;
}
static JSValue js_vb_shoot(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    if (argc < 2) return JS_ThrowTypeError(ctx, "vb.shoot(sprite, tile)");
    int32_t sprite = 0, tile = 0; JS_ToInt32(ctx, &sprite, argv[0]); JS_ToInt32(ctx, &tile, argv[1]);
    return JS_NewInt32(ctx, vbjs_game::shoot(sprite, tile));
}
static JSValue js_vb_addAmmo(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    if (argc < 3) return JS_ThrowTypeError(ctx, "vb.addAmmo(player, weapon, amount)");
    int32_t pl = 0, w = 0, amt = 0; JS_ToInt32(ctx, &pl, argv[0]); JS_ToInt32(ctx, &w, argv[1]); JS_ToInt32(ctx, &amt, argv[2]);
    vbjs_game::addAmmo(pl, w, amt);
    return JS_UNDEFINED;
}
static JSValue js_vb_addWeapon(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    if (argc < 2) return JS_ThrowTypeError(ctx, "vb.addWeapon(player, weapon[, switchTo])");
    int32_t pl = 0, w = 0, sw = 1; JS_ToInt32(ctx, &pl, argv[0]); JS_ToInt32(ctx, &w, argv[1]);
    if (argc > 2) JS_ToInt32(ctx, &sw, argv[2]);
    vbjs_game::addWeapon(pl, w, sw);
    return JS_UNDEFINED;
}

static JSValue js_vb_define(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    if (argc < 1) return JS_ThrowTypeError(ctx, "vb.define(name)");
    const char *name = JS_ToCString(ctx, argv[0]);
    if (!name) return JS_EXCEPTION;
    int ok = 0; int v = vbjs_game::conDefine(name, &ok);
    JS_FreeCString(ctx, name);
    return ok ? JS_NewInt32(ctx, v) : JS_UNDEFINED;
}

static JSValue js_vb_player(JSContext *ctx, JSValueConst, int, JSValueConst *)
{
    int sp = vbjs_game::playerSprite();
    JSValue o = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, o, "sprite", JS_NewInt32(ctx, sp));
    int x = 0, y = 0, z = 0, ok = 0;
    if (sp >= 0) vbjs_game::spriteXYZ(sp, &x, &y, &z, &ok);
    JS_SetPropertyStr(ctx, o, "x", JS_NewInt32(ctx, x));
    JS_SetPropertyStr(ctx, o, "y", JS_NewInt32(ctx, y));
    JS_SetPropertyStr(ctx, o, "z", JS_NewInt32(ctx, z));
    return o;
}

// --------------------------------------------------- vb/fs export helpers ---

// Attach every vb function to the given object (used for both the module and the global).
static void vbjs_define_vb(JSContext *ctx, JSValue target)
{
    JS_SetPropertyStr(ctx, target, "on",          JS_NewCFunction(ctx, js_vb_on,          "on", 2));
    JS_SetPropertyStr(ctx, target, "off",         JS_NewCFunction(ctx, js_vb_off,         "off", 1));
    JS_SetPropertyStr(ctx, target, "log",         JS_NewCFunction(ctx, js_console_log,    "log", 1));
    JS_SetPropertyStr(ctx, target, "drawText",    JS_NewCFunction(ctx, js_vb_drawText,    "drawText", 4));
    JS_SetPropertyStr(ctx, target, "clearText",   JS_NewCFunction(ctx, js_vb_clearText,   "clearText", 1));
    JS_SetPropertyStr(ctx, target, "breakpoint",  JS_NewCFunction(ctx, js_vb_breakpoint,  "breakpoint", 0));
    JS_SetPropertyStr(ctx, target, "spawn",       JS_NewCFunction(ctx, js_vb_spawn,       "spawn", 4));
    JS_SetPropertyStr(ctx, target, "A_Spawn",     JS_NewCFunction(ctx, js_vb_A_Spawn,     "A_Spawn", 2));
    JS_SetPropertyStr(ctx, target, "A_InsertSprite", JS_NewCFunction(ctx, js_vb_A_InsertSprite, "A_InsertSprite", 13));
    JS_SetPropertyStr(ctx, target, "sendMessage", JS_NewCFunction(ctx, js_vb_sendMessage, "sendMessage", 1));
    JS_SetPropertyStr(ctx, target, "getGamevar",  JS_NewCFunction(ctx, js_vb_getGamevar,  "getGamevar", 1));
    JS_SetPropertyStr(ctx, target, "setGamevar",  JS_NewCFunction(ctx, js_vb_setGamevar,  "setGamevar", 2));
    JS_SetPropertyStr(ctx, target, "player",      JS_NewCFunction(ctx, js_vb_player,      "player", 0));
    JS_SetPropertyStr(ctx, target, "run",         JS_NewCFunction(ctx, js_cvar_run,       "run", 1));   // alias of cvar.run
    JS_SetPropertyStr(ctx, target, "define",      JS_NewCFunction(ctx, js_vb_define,      "define", 1));
    JS_SetPropertyStr(ctx, target, "sound",       JS_NewCFunction(ctx, js_vb_sound,      "sound", 1));
    JS_SetPropertyStr(ctx, target, "shoot",       JS_NewCFunction(ctx, js_vb_shoot,      "shoot", 2));
    JS_SetPropertyStr(ctx, target, "addAmmo",     JS_NewCFunction(ctx, js_vb_addAmmo,    "addAmmo", 3));
    JS_SetPropertyStr(ctx, target, "addWeapon",   JS_NewCFunction(ctx, js_vb_addWeapon,  "addWeapon", 2));

    // Generic CON struct data access — every member CON exposes, by name.
    struct { const char *get, *set; int kind; } const acc[] = {
        { "getActor","setActor", VBJS_ACTOR }, { "getPlayer","setPlayer", VBJS_PLAYER },
        { "getInput","setInput", VBJS_INPUT }, { "getSector","setSector", VBJS_SECTOR },
        { "getWall","setWall", VBJS_WALL }, { "getProjectile","setProjectile", VBJS_PROJECTILE },
        { "getActiveProjectile","setActiveProjectile", VBJS_ACTIVEPROJ },
        { "getTileData","setTileData", VBJS_TILEDATA }, { "getUserdef","setUserdef", VBJS_USERDEF },
    };
    for (auto const &a : acc)
    {
        JS_SetPropertyStr(ctx, target, a.get, JS_NewCFunctionMagic(ctx, js_con_get, a.get, 2, JS_CFUNC_generic_magic, a.kind));
        JS_SetPropertyStr(ctx, target, a.set, JS_NewCFunctionMagic(ctx, js_con_set, a.set, 3, JS_CFUNC_generic_magic, a.kind));
    }
}
static void vbjs_define_cvar(JSContext *ctx, JSValue target)
{
    JS_SetPropertyStr(ctx, target, "run", JS_NewCFunction(ctx, js_cvar_run, "run", 1));
    JS_SetPropertyStr(ctx, target, "set", JS_NewCFunction(ctx, js_cvar_set, "set", 2));
}
static void vbjs_define_fs(JSContext *ctx, JSValue target)
{
    JS_SetPropertyStr(ctx, target, "readFile",   JS_NewCFunction(ctx, js_fs_readFile,   "readFile", 1));
    JS_SetPropertyStr(ctx, target, "writeFile",  JS_NewCFunction(ctx, js_fs_writeFile,  "writeFile", 2));
    JS_SetPropertyStr(ctx, target, "appendFile", JS_NewCFunction(ctx, js_fs_appendFile, "appendFile", 2));
    JS_SetPropertyStr(ctx, target, "exists",     JS_NewCFunction(ctx, js_fs_exists,     "exists", 1));
}

// ES-module init for `import ... from 'vb'` / 'fs' (default + named exports).
static const char *k_vb_exports[] = {
    "on","off","log","drawText","clearText","breakpoint","spawn","A_Spawn","A_InsertSprite","sendMessage","getGamevar","setGamevar","player","run","define",
    "sound","shoot","addAmmo","addWeapon",
    "getActor","setActor","getPlayer","setPlayer","getInput","setInput","getSector","setSector","getWall","setWall",
    "getProjectile","setProjectile","getActiveProjectile","setActiveProjectile","getTileData","setTileData","getUserdef","setUserdef",
};
static const char *k_fs_exports[] = { "readFile","writeFile","appendFile","exists" };
static const char *k_cvar_exports[] = { "run","set" };

static int vb_module_init(JSContext *ctx, JSModuleDef *m)
{
    JSValue o = JS_NewObject(ctx); vbjs_define_vb(ctx, o);
    for (auto n : k_vb_exports) JS_SetModuleExport(ctx, m, n, JS_GetPropertyStr(ctx, o, n));
    JS_SetModuleExport(ctx, m, "default", o);
    return 0;
}
static int fs_module_init(JSContext *ctx, JSModuleDef *m)
{
    JSValue o = JS_NewObject(ctx); vbjs_define_fs(ctx, o);
    for (auto n : k_fs_exports) JS_SetModuleExport(ctx, m, n, JS_GetPropertyStr(ctx, o, n));
    JS_SetModuleExport(ctx, m, "default", o);
    return 0;
}
static int cvar_module_init(JSContext *ctx, JSModuleDef *m)
{
    JSValue o = JS_NewObject(ctx); vbjs_define_cvar(ctx, o);
    for (auto n : k_cvar_exports) JS_SetModuleExport(ctx, m, n, JS_GetPropertyStr(ctx, o, n));
    JS_SetModuleExport(ctx, m, "default", o);
    return 0;
}

// ------------------------------------------------------- CommonJS require() ---

static JSValue s_requireCache;  // { resolvedPath: moduleExports }

static JSValue js_require(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    if (argc < 1) return JS_ThrowTypeError(ctx, "require(path)");
    const char *name = JS_ToCString(ctx, argv[0]);
    if (!name) return JS_EXCEPTION;

    // Builtins are also require-able.
    if (!strcmp(name, "vb") || !strcmp(name, "fs") || !strcmp(name, "cvar"))
    {
        JSValue o = JS_NewObject(ctx);
        if (!strcmp(name, "vb")) vbjs_define_vb(ctx, o);
        else if (!strcmp(name, "fs")) vbjs_define_fs(ctx, o);
        else vbjs_define_cvar(ctx, o);
        JS_FreeCString(ctx, name);
        return o;
    }

    char path[1024]; vbjs_resolve(name, path, sizeof(path));
    JS_FreeCString(ctx, name);

    JSValue cached = JS_GetPropertyStr(ctx, s_requireCache, path);
    if (!JS_IsUndefined(cached)) return cached;
    JS_FreeValue(ctx, cached);

    size_t len = 0; char *src = vbjs_load_source(path, &len);
    if (!src) return JS_ThrowReferenceError(ctx, "require: cannot find module '%s'", path);

    // CommonJS wrapper: (function(exports, module, require){ <src> }); return module.exports
    char *wrap = (char *)malloc(len + 96);
    int wlen = snprintf(wrap, len + 96, "(function(exports, module, require){%s\n});", src);
    free(src);
    JSValue fn = JS_Eval(ctx, wrap, (size_t)wlen, path, JS_EVAL_TYPE_GLOBAL);
    free(wrap);
    if (JS_IsException(fn)) { vbjs_dump_error(ctx); return fn; }

    JSValue module  = JS_NewObject(ctx);
    JSValue exports  = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, module, "exports", JS_DupValue(ctx, exports));
    JSValue reqfn = JS_NewCFunction(ctx, js_require, "require", 1);
    JSValueConst args[3] = { exports, module, reqfn };
    JSValue r = JS_Call(ctx, fn, JS_UNDEFINED, 3, args);
    JS_FreeValue(ctx, fn); JS_FreeValue(ctx, reqfn);
    if (JS_IsException(r)) { vbjs_dump_error(ctx); JS_FreeValue(ctx, exports); JS_FreeValue(ctx, module); return r; }
    JS_FreeValue(ctx, r);

    JSValue result = JS_GetPropertyStr(ctx, module, "exports");  // module.exports may have been replaced
    JS_FreeValue(ctx, exports);
    JS_FreeValue(ctx, module);
    JS_SetPropertyStr(ctx, s_requireCache, path, JS_DupValue(ctx, result));
    return result;
}

// ------------------------------------------------------- ES module loader ---

static char *vbjs_normalize(JSContext *ctx, const char *base, const char *name, void *)
{
    if (!strcmp(name, "vb") || !strcmp(name, "fs") || !strcmp(name, "cvar"))
        return js_strdup(ctx, name);
    // Relative to the importing module's directory when possible, else the script dir.
    char resolved[1024];
    if (name[0] == '/') snprintf(resolved, sizeof(resolved), "%s", name);
    else
    {
        char basedir[1024]; snprintf(basedir, sizeof(basedir), "%s", base ? base : "");
        char *slash = strrchr(basedir, '/');
        if (slash) { *slash = 0; snprintf(resolved, sizeof(resolved), "%s/%s", basedir, name); }
        else vbjs_resolve(name, resolved, sizeof(resolved));
    }
    return js_strdup(ctx, resolved);
}

static JSModuleDef *vbjs_module_loader(JSContext *ctx, const char *module_name, void *)
{
    size_t len = 0; char *buf = vbjs_load_source(module_name, &len);
    if (!buf) { JS_ThrowReferenceError(ctx, "could not load module '%s'", module_name); return nullptr; }
    JSValue fv = JS_Eval(ctx, buf, len, module_name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    free(buf);
    if (JS_IsException(fv)) return nullptr;
    JSModuleDef *m = (JSModuleDef *)JS_VALUE_GET_PTR(fv);
    JS_FreeValue(ctx, fv);
    return m;
}

// ----------------------------------------------------------------- runtime ---

static void vbjs_setup_context(JSContext *ctx)
{
    s_events       = JS_NewObject(ctx);
    s_requireCache = JS_NewObject(ctx);

    JSValue g = JS_GetGlobalObject(ctx);

    // console
    JSValue console = JS_NewObject(ctx);
    JSValue logfn = JS_NewCFunction(ctx, js_console_log, "log", 1);
    JS_SetPropertyStr(ctx, console, "log",   JS_DupValue(ctx, logfn));
    JS_SetPropertyStr(ctx, console, "error", JS_DupValue(ctx, logfn));
    JS_SetPropertyStr(ctx, console, "warn",  JS_DupValue(ctx, logfn));
    JS_SetPropertyStr(ctx, console, "info",  logfn);
    JS_SetPropertyStr(ctx, g, "console", console);

    // Global require + a global `vb` object for compatibility with simple scripts.
    // The preferred API is the explicit `vb` ES module.
    JS_SetPropertyStr(ctx, g, "require", JS_NewCFunction(ctx, js_require, "require", 1));
    JSValue vb = JS_NewObject(ctx); vbjs_define_vb(ctx, vb);
    JS_SetPropertyStr(ctx, g, "vb", vb);
    JSValue cvar = JS_NewObject(ctx); vbjs_define_cvar(ctx, cvar);
    JS_SetPropertyStr(ctx, g, "cvar", cvar);

    JS_FreeValue(ctx, g);

    // native ES modules
    JS_SetModuleLoaderFunc(s_rt, vbjs_normalize, vbjs_module_loader, nullptr);
    JSModuleDef *mvb = JS_NewCModule(ctx, "vb", vb_module_init);
    for (auto n : k_vb_exports) JS_AddModuleExport(ctx, mvb, n);
    JS_AddModuleExport(ctx, mvb, "default");
    JSModuleDef *mfs = JS_NewCModule(ctx, "fs", fs_module_init);
    for (auto n : k_fs_exports) JS_AddModuleExport(ctx, mfs, n);
    JS_AddModuleExport(ctx, mfs, "default");
    JSModuleDef *mcvar = JS_NewCModule(ctx, "cvar", cvar_module_init);
    for (auto n : k_cvar_exports) JS_AddModuleExport(ctx, mcvar, n);
    JS_AddModuleExport(ctx, mcvar, "default");
}

// ------------------------------------------------------------- public C API ---

extern "C" {

int VBJS_IsEnabled(void) { return s_enabled; }

void VBJS_CaptureLaunchDir(void) { if (!s_launchDir[0]) getcwd(s_launchDir, sizeof(s_launchDir)); }

void VBJS_SetScriptPath(const char *path) { vbjs_add_script(path); }

void VBJS_SetDevtools(int enabled) { s_devtools = enabled != 0; }
int VBJS_DevtoolsEnabled(void) { return s_devtools; }
void VBJS_DevtoolsPump(void)
{
    if (!s_ctx) return;
    js_debugger_cooperate(s_ctx);
}
int VBJS_DevtoolsDebuggerSend(const char *json)
{
    if (!json || !json[0]) return 0;
    size_t length = strlen(json); char header[10]; snprintf(header, sizeof(header), "%08x\n", (unsigned)(length + 1));
    pthread_mutex_lock(&s_debugTransport.mutex);
    for (int i = 0; i < 9; ++i) s_debugTransport.inbound.push_back(header[i]);
    for (size_t i = 0; i < length; ++i) s_debugTransport.inbound.push_back(json[i]);
    s_debugTransport.inbound.push_back('\n'); pthread_cond_broadcast(&s_debugTransport.changed); pthread_mutex_unlock(&s_debugTransport.mutex);
    return 1;
}
int VBJS_DevtoolsDebuggerReceiveForClient(int client, char *out, int outSize)
{
    if (!out || outSize < 2) return 0;
    client = client == 1 ? 1 : 0;
    pthread_mutex_lock(&s_debugTransport.mutex);
    if (s_debugTransport.outbound[client].size() < 9) { pthread_mutex_unlock(&s_debugTransport.mutex); return 0; }
    char header[10]; for (int i = 0; i < 9; ++i) header[i] = s_debugTransport.outbound[client][i]; header[8] = 0;
    unsigned length = 0; if (sscanf(header, "%x", &length) != 1 || !length || s_debugTransport.outbound[client].size() < 9u + length) { pthread_mutex_unlock(&s_debugTransport.mutex); return 0; }
    size_t copied = length > (unsigned)outSize - 1 ? (unsigned)outSize - 1 : length - 1;
    for (int i = 0; i < 9; ++i) s_debugTransport.outbound[client].pop_front();
    for (unsigned i = 0; i < length; ++i) { char c = s_debugTransport.outbound[client].front(); s_debugTransport.outbound[client].pop_front(); if (i < copied) out[i] = c; }
    out[copied] = 0; pthread_mutex_unlock(&s_debugTransport.mutex); return (int)copied;
}
int VBJS_DevtoolsDebuggerReceive(char *out, int outSize) { return VBJS_DevtoolsDebuggerReceiveForClient(0, out, outSize); }
const char *VBJS_DevtoolsLog(int index)
{
    if (index < 0 || index >= VBJS_DEVTOOLS_LOGS || index >= s_devtoolsLogCount) return nullptr;
    int first = s_devtoolsLogCount > VBJS_DEVTOOLS_LOGS ? s_devtoolsLogCount - VBJS_DEVTOOLS_LOGS : 0;
    if (index + first >= s_devtoolsLogCount) return nullptr;
    return s_devtoolsLogs[(first + index) % VBJS_DEVTOOLS_LOGS];
}
int VBJS_DevtoolsPaused(void) { return s_devtoolsPaused; }
void VBJS_DevtoolsResume(void) { s_devtoolsPaused = 0; }
const char *VBJS_DevtoolsCurrentEvent(void) { return s_devtoolsCurrentEvent[0] ? s_devtoolsCurrentEvent : nullptr; }
int VBJS_DevtoolsEvaluate(const char *expression, char *out, int outSize)
{
    if (!out || outSize <= 0) return -1;
    out[0] = 0;
    if (!s_ctx || !expression || !expression[0]) return -1;
    JSValue value;
    if (!strcmp(expression, "$event")) value = JS_DupValue(s_ctx, s_devtoolsEvent);
    else value = JS_Eval(s_ctx, expression, strlen(expression), "<devtools>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(value)) { vbjs_dump_error(s_ctx); snprintf(out, (size_t)outSize, "<exception>"); return -1; }
    JSValue json = JS_JSONStringify(s_ctx, value, JS_NULL, JS_UNDEFINED);
    const char *text = JS_IsException(json) || JS_IsUndefined(json) ? JS_ToCString(s_ctx, value) : JS_ToCString(s_ctx, json);
    snprintf(out, (size_t)outSize, "%s", text ? text : "undefined");
    if (text) JS_FreeCString(s_ctx, text);
    JS_FreeValue(s_ctx, json);
    JS_FreeValue(s_ctx, value);
    return 0;
}

int VBJS_DevtoolsSourceCount(void) { return s_scriptCount; }
const char *VBJS_DevtoolsSourcePath(int index) { return (unsigned)index < (unsigned)s_scriptCount ? s_scripts[index].path : nullptr; }
int VBJS_DevtoolsReadSource(int index, char *out, int outSize)
{
    if (!out || outSize < 1 || (unsigned)index >= (unsigned)s_scriptCount) return 0;
    size_t length = 0; char *source = vbjs_load_source(s_scripts[index].path, &length);
    if (!source) { out[0] = 0; return 0; }
    snprintf(out, outSize, "%s", source); free(source); return 1;
}

int VBJS_GenerateProject(const char *requestedName, int typescript)
{
    VBJS_CaptureLaunchDir();
    if (!s_launchDir[0]) return -1;

    char name[96] = {0};
    if (requestedName)
    {
        if (vbjs_safe_project_name(requestedName, name, sizeof(name)) != 0) return -1;
    }
    else
    {
        for (unsigned int n = 1; n < 10000; ++n)
        {
            snprintf(name, sizeof(name), "mod-%u", n);
            char candidate[1200]; struct stat st;
            snprintf(candidate, sizeof(candidate), "%s/autojs/%s", s_launchDir, name);
            if (stat(candidate, &st) != 0 && errno == ENOENT) break;
        }
    }

    char autojs[1100], project[1200];
    snprintf(autojs, sizeof(autojs), "%s/autojs", s_launchDir);
    if (mkdir(autojs, 0755) != 0 && errno != EEXIST) return -1;
    snprintf(project, sizeof(project), "%s/%s", autojs, name);
    if (mkdir(project, 0755) != 0) return -1; // never overwrite a project

    char path[1400];
    const char *packageTs =
        "{\n"
        "  \"name\": \"vbjs-mod\",\n"
        "  \"private\": true,\n"
        "  \"version\": \"0.1.0\",\n"
        "  \"description\": \"VibeBuild32 auto-loaded JavaScript mod\",\n"
        "  \"types\": \"./vb.d.ts\",\n"
        "  \"scripts\": { \"check\": \"tsc --noEmit -p tsconfig.json\" },\n"
        "  \"devDependencies\": { \"typescript\": \"^5.0.0\" }\n"
        "}\n";
    const char *packageJs =
        "{\n"
        "  \"name\": \"vbjs-mod\",\n"
        "  \"private\": true,\n"
        "  \"version\": \"0.1.0\",\n"
        "  \"description\": \"VibeBuild32 auto-loaded JavaScript mod\",\n"
        "  \"types\": \"./vb.d.ts\",\n"
        "  \"scripts\": { \"check\": \"tsc --noEmit -p jsconfig.json\" },\n"
        "  \"devDependencies\": { \"typescript\": \"^5.0.0\" }\n"
        "}\n";
    const char *tsconfig =
        "{\n"
        "  \"compilerOptions\": {\n"
        "    \"target\": \"ES2020\", \"lib\": [\"ES2020\"], \"module\": \"ESNext\",\n"
        "    \"moduleResolution\": \"Bundler\", \"allowImportingTsExtensions\": true, \"strict\": true,\n"
        "    \"noEmit\": true, \"skipLibCheck\": true\n"
        "  },\n"
        "  \"include\": [\"*.ts\", \"vb.d.ts\"]\n"
        "}\n";
    const char *indexTs =
        "// VibeBuild32 auto-loads only index.ts (or index.js) from this project.\n"
        "import { on, log, drawText, define } from 'vb';\n"
        "import fs from 'fs';\n"
        "import cvar from 'cvar';\n"
        "const vbLegacy = require('vb'); // CommonJS require() is supported too.\n\n"
        "let frames = 0;\n"
        "const liztroop = define('LIZTROOP');\n\n"
        "on('start', () => {\n"
        "  log('TypeScript mod loaded. LIZTROOP is', liztroop, 'player:', vbLegacy.player().sprite);\n"
        "  // Built-in modules are typed too; this is safe even when the file does not exist.\n"
        "  if (fs.exists('autojs-notes.txt')) log(fs.readFile('autojs-notes.txt'));\n"
        "  cvar.set('r_maxfps', 60);\n"
        "});\n\n"
        "on('actorSpawned', event => {\n"
        "  if (event.tile === liztroop) log('A trooper spawned:', event.sprite);\n"
        "});\n\n"
        "on('frame', () => {\n"
        "  if (++frames % 60 === 0) drawText(9001, 8, 8, `TS mod: ${frames} frames`);\n"
        "});\n";
    const char *indexJs =
        "// VibeBuild32 auto-loads only index.js (or index.ts) from this project.\n"
        "// @ts-check\n"
        "/** @type {import('vb').default} */\n"
        "const vb = require('vb');\n"
        "const fs = require('fs');\n"
        "const cvar = require('cvar');\n\n"
        "let frames = 0;\n"
        "const liztroop = vb.define('LIZTROOP');\n\n"
        "vb.on('start', () => {\n"
        "  vb.log('JavaScript mod loaded. LIZTROOP is', liztroop);\n"
        "  if (fs.exists('autojs-notes.txt')) vb.log(fs.readFile('autojs-notes.txt'));\n"
        "  cvar.set('r_maxfps', 60);\n"
        "});\n\n"
        "vb.on('actorSpawned', event => {\n"
        "  if (event.tile === liztroop) vb.log('A trooper spawned:', event.sprite);\n"
        "});\n\n"
        "vb.on('frame', () => {\n"
        "  if (++frames % 60 === 0) vb.drawText(9001, 8, 8, `JS mod: ${frames} frames`);\n"
        "});\n";
    const char *jsconfig =
        "{\n"
        "  \"compilerOptions\": { \"target\": \"ES2020\", \"lib\": [\"ES2020\"], \"checkJs\": true, \"allowJs\": true, \"noEmit\": true, \"strict\": true },\n"
        "  \"include\": [\"*.js\", \"vb.d.ts\"]\n"
        "}\n";
    const char *types =
        "// VibeBuild32 embedded JavaScript API declarations.\n"
        "declare type ActorEvent = { sprite: number; owner: number; tile: number };\n"
        "declare type ChatEvent = { player: number; text: string };\n"
        "declare type ConEvent = { sprite: number; player: number; dist: number };\n"
        "declare type Player = { sprite: number; x: number; y: number; z: number };\n"
        "declare type ConValue = number | undefined;\n"
        "declare interface VBConsole { log(...args: unknown[]): void; error(...args: unknown[]): void; warn(...args: unknown[]): void; info(...args: unknown[]): void; }\n"
        "// Every CON EVENT_* name accepted by vb.on(). The prefix is optional at runtime.\n"
        "declare type ConEventShortName =\n"
        "  | 'ACTIVATESWITCH' | 'AIMDOWN' | 'AIMUP' | 'ALTFIRE' | 'ALTWEAPON' | 'ANIMATESPRITES'\n"
        "  | 'BOTGETSPRITESCORE' | 'CHANGEWEAPON' | 'CHEATGETBOOT' | 'CHEATGETFIRSTAID' | 'CHEATGETHEAT'\n"
        "  | 'CHEATGETHOLODUKE' | 'CHEATGETJETPACK' | 'CHEATGETSCUBA' | 'CHEATGETSHIELD' | 'CHEATGETSTEROIDS'\n"
        "  | 'CHECKTOUCHDAMAGE' | 'CROUCH' | 'DAMAGECEILING' | 'DAMAGEFLOOR' | 'DAMAGESPRITE'\n"
        "  | 'DISPLAYACCESS' | 'DISPLAYBONUSSCREEN' | 'DISPLAYCROSSHAIR' | 'DISPLAYLEVELSTATS'\n"
        "  | 'DISPLAYLOADINGSCREEN' | 'DISPLAYMENU' | 'DISPLAYMENUREST' | 'DISPLAYREST' | 'DISPLAYROOMS'\n"
        "  | 'DISPLAYSBAR' | 'DISPLAYSTART' | 'DISPLAYTIP' | 'DISPLAYWEAPON' | 'DOFIRE' | 'DOQUAKE'\n"
        "  | 'DRAWWEAPON' | 'EGS' | 'ENTERLEVEL' | 'FAKEDOMOVETHINGS' | 'FIRE' | 'FIREWEAPON'\n"
        "  | 'FRAGGED' | 'GAME' | 'GETAUTOAIMANGLE' | 'GETLOADTILE' | 'GETMENUTILE' | 'GETSHOTRANGE'\n"
        "  | 'HOLODUKEOFF' | 'HOLODUKEON' | 'HOLSTER' | 'INCURDAMAGE' | 'INIT' | 'INVENTORY'\n"
        "  | 'INVENTORYLEFT' | 'INVENTORYRIGHT' | 'JUMP' | 'KILLIT' | 'LASTWEAPON' | 'LOADACTOR'\n"
        "  | 'LOGO' | 'LOOKDOWN' | 'LOOKLEFT' | 'LOOKRIGHT' | 'LOOKUP' | 'MOVEBACKWARD' | 'MOVEFORWARD'\n"
        "  | 'NEWGAME' | 'NEXTWEAPON' | 'POSTDAMAGESPRITE' | 'POSTWEAPONSHOOT' | 'PREGAME' | 'PRELEVEL'\n"
        "  | 'PRESSEDFIRE' | 'PREVIOUSWEAPON' | 'PREWEAPONSHOOT' | 'PREWORLD' | 'PROCESSINPUT'\n"
        "  | 'QUICKKICK' | 'RESETINVENTORY' | 'RESETPLAYER' | 'RESETWEAPONS' | 'RETURNTOCENTER'\n"
        "  | 'SELECTWEAPON' | 'SETDEFAULTS' | 'SOARDOWN' | 'SOARUP' | 'SOUND' | 'SPAWN'\n"
        "  | 'STRAFELEFT' | 'STRAFERIGHT' | 'SWIMDOWN' | 'SWIMUP' | 'TURNAROUND' | 'TURNLEFT'\n"
        "  | 'TURNRIGHT' | 'USE' | 'USEJETPACK' | 'USEMEDKIT' | 'USENIGHTVISION' | 'USESTEROIDS'\n"
        "  | 'WEAPKEY1' | 'WEAPKEY2' | 'WEAPKEY3' | 'WEAPKEY4' | 'WEAPKEY5' | 'WEAPKEY6'\n"
        "  | 'WEAPKEY7' | 'WEAPKEY8' | 'WEAPKEY9' | 'WEAPKEY10' | 'WORLD';\n"
        "declare type ConEventName = ConEventShortName | `EVENT_${ConEventShortName}`;\n"
        "declare type VBLifecycleEventName = 'start' | 'shutdown' | 'frame' | 'playerDied';\n"
        "declare type VBEventName = VBLifecycleEventName | 'actorSpawned' | 'chatMessage' | ConEventName;\n"
        "declare type VBEventMap = {\n"
        "  start: void; shutdown: void; frame: void; playerDied: void;\n"
        "  actorSpawned: ActorEvent; chatMessage: ChatEvent;\n"
        "} & { [K in ConEventName]: ConEvent };\n"
        "declare interface VB {\n"
        "  on<K extends VBEventName>(name: K, handler: (event: VBEventMap[K]) => void): void;\n"
        "  /** Supports engine/plugin events that are not part of the built-in event list. */\n"
        "  on(name: string, handler: (event?: unknown) => void): void;\n"
        "  off<K extends VBEventName>(name?: K): void;\n"
        "  off(name?: string): void;\n"
        "  log(...args: unknown[]): void; drawText(id: number, x: number, y: number, text: string, pal?: number, shade?: number): void; clearText(id?: number): void; breakpoint(): void;\n"
        "  spawn(tile: number, x?: number, y?: number, z?: number, owner?: number): number;\n"
        "  A_Spawn(spriteNum: number, tileNum: number): number;\n"
        "  A_InsertSprite(whatsect: number, s_x: number, s_y: number, s_z: number, s_pn: number, s_s: number, s_xr: number, s_yr: number, s_a: number, s_ve: number, s_zv: number, s_ow: number, s_ss: number): number;\n"
        "  sendMessage(text: string): void; player(): Player;\n"
        "  getGamevar(name: string, sprite?: number, player?: number): ConValue; setGamevar(name: string, value: number, sprite?: number, player?: number): void;\n"
        "  getActor(i: number, member: string, parm2?: number): ConValue; setActor(i: number, member: string, value: number, parm2?: number): void;\n"
        "  getPlayer(i: number, member: string, parm2?: number): ConValue; setPlayer(i: number, member: string, value: number, parm2?: number): void;\n"
        "  getInput(i: number, member: string): ConValue; setInput(i: number, member: string, value: number): void;\n"
        "  getSector(i: number, member: string, parm2?: number): ConValue; setSector(i: number, member: string, value: number, parm2?: number): void; getWall(i: number, member: string, parm2?: number): ConValue; setWall(i: number, member: string, value: number, parm2?: number): void;\n"
        "  getProjectile(i: number, member: string, parm2?: number): ConValue; setProjectile(i: number, member: string, value: number, parm2?: number): void; getActiveProjectile(i: number, member: string, parm2?: number): ConValue; setActiveProjectile(i: number, member: string, value: number, parm2?: number): void; getTileData(i: number, member: string, parm2?: number): ConValue; setTileData(i: number, member: string, value: number, parm2?: number): void;\n"
        "  getUserdef(member: string, parm2?: number): ConValue; setUserdef(member: string, value: number, parm2?: number): void; define(name: string): ConValue;\n"
        "  sound(num: number, sprite?: number): void; shoot(sprite: number, tile: number): number; addAmmo(player: number, weapon: number, amount: number): void; addWeapon(player: number, weapon: number, switchTo?: number): void; run(command: string): void;\n"
        "}\n"
        "declare interface FS { readFile(path: string): string; writeFile(path: string, data: string): void; appendFile(path: string, data: string): void; exists(path: string): boolean; }\n"
        "declare interface CVar { run(command: string): void; set(name: string, value: unknown): void; }\n"
        "declare const console: VBConsole; declare const vb: VB; declare const cvar: CVar;\n"
        "declare function require(name: 'vb'): VB; declare function require(name: 'fs'): FS; declare function require(name: 'cvar'): CVar; declare function require(name: string): unknown;\n"
        "declare module 'vb' { export type { ActorEvent, ChatEvent, ConEvent, ConEventName, ConEventShortName, ConValue, Player, VBEventMap, VBEventName, VBLifecycleEventName, VBConsole }; const api: VB; export default api; export const on: VB['on']; export const off: VB['off']; export const log: VB['log']; export const drawText: VB['drawText']; export const clearText: VB['clearText']; export const breakpoint: VB['breakpoint']; export const spawn: VB['spawn']; export const A_Spawn: VB['A_Spawn']; export const A_InsertSprite: VB['A_InsertSprite']; export const sendMessage: VB['sendMessage']; export const player: VB['player']; export const getGamevar: VB['getGamevar']; export const setGamevar: VB['setGamevar']; export const run: VB['run']; export const define: VB['define']; export const sound: VB['sound']; export const shoot: VB['shoot']; export const addAmmo: VB['addAmmo']; export const addWeapon: VB['addWeapon']; export const getActor: VB['getActor']; export const setActor: VB['setActor']; export const getPlayer: VB['getPlayer']; export const setPlayer: VB['setPlayer']; export const getInput: VB['getInput']; export const setInput: VB['setInput']; export const getSector: VB['getSector']; export const setSector: VB['setSector']; export const getWall: VB['getWall']; export const setWall: VB['setWall']; export const getProjectile: VB['getProjectile']; export const setProjectile: VB['setProjectile']; export const getActiveProjectile: VB['getActiveProjectile']; export const setActiveProjectile: VB['setActiveProjectile']; export const getTileData: VB['getTileData']; export const setTileData: VB['setTileData']; export const getUserdef: VB['getUserdef']; export const setUserdef: VB['setUserdef']; }\n"
        "declare module 'fs' { const api: FS; export default api; export const readFile: FS['readFile']; export const writeFile: FS['writeFile']; export const appendFile: FS['appendFile']; export const exists: FS['exists']; }\n"
        "declare module 'cvar' { const api: CVar; export default api; export const run: CVar['run']; export const set: CVar['set']; }\n";
    snprintf(path, sizeof(path), "%s/package.json", project); if (vbjs_write_text(path, typescript ? packageTs : packageJs)) return -1;
    snprintf(path, sizeof(path), "%s/%sconfig.json", project, typescript ? "ts" : "js");
    if (vbjs_write_text(path, typescript ? tsconfig : jsconfig)) return -1;
    snprintf(path, sizeof(path), "%s/index.%s", project, typescript ? "ts" : "js");
    if (vbjs_write_text(path, typescript ? indexTs : indexJs)) return -1;
    snprintf(path, sizeof(path), "%s/vb.d.ts", project); if (vbjs_write_text(path, types)) return -1;
    // Bootstrap the exact VS Code DAP setup used by the bundled starter mod.
    // It works for either index.ts or index.js because the adapter receives
    // source paths directly from VS Code.
    char vscode[1400]; snprintf(vscode, sizeof(vscode), "%s/.vscode", project);
    if (mkdir(vscode, 0755) != 0) return -1;
    const char *vscodeFiles[] = { "launch.json", "tasks.json", "README.md", "vibebuild32-debug-adapter.js" };
    for (auto file : vscodeFiles)
    {
        char source[1400], target[1400]; size_t length = 0;
        snprintf(source, sizeof(source), "%s/autojs/my-mod/.vscode/%s", s_launchDir, file);
        char *contents = vbjs_read_file(source, &length);
        if (!contents) return -1;
        snprintf(target, sizeof(target), "%s/%s", vscode, file);
        int failed = vbjs_write_text(target, contents); free(contents);
        if (failed) return -1;
    }
    fprintf(stdout, "Created JavaScript project: %s\n", project);
    return 0;
}

// Scan <base>/autojs/<name>/index.{ts,js} — exactly one folder deep — and queue each
// (full paths, since the game's CWD is elsewhere).
static void vbjs_scan_autojs_in(const char *base)
{
    if (!base || !base[0]) return;
    char adir[1024]; snprintf(adir, sizeof(adir), "%s/autojs", base);
    DIR *d = opendir(adir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != nullptr)
    {
        if (e->d_name[0] == '.') continue;
        char sub[1200]; snprintf(sub, sizeof(sub), "%s/%s", adir, e->d_name);
        struct stat st;
        if (stat(sub, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        char ts[1300], js[1300]; struct stat s2;
        snprintf(ts, sizeof(ts), "%s/index.ts", sub);
        snprintf(js, sizeof(js), "%s/index.js", sub);
        if      (stat(ts, &s2) == 0) vbjs_add_script(ts);   // TypeScript preferred
        else if (stat(js, &s2) == 0) vbjs_add_script(js);
    }
    closedir(d);
}

static void vbjs_scan_autojs(void)
{
    vbjs_scan_autojs_in(s_launchDir);   // the game's data/launch dir (holds autojs/)
    char cwd[1024]; if (getcwd(cwd, sizeof(cwd)) && strcmp(cwd, s_launchDir)) vbjs_scan_autojs_in(cwd);
}

void VBJS_Init(void)
{
    if (s_ctx || s_initTried) return;
    s_initTried = 1;

    vbjs_scan_autojs();                 // add autojs/<name>/index.{ts,js}
    if (s_scriptCount == 0) return;     // nothing to run

    s_rt = JS_NewRuntime();
    if (!s_rt) { vbjs_game::log("[js] failed to create runtime"); return; }
    s_ctx = JS_NewContext(s_rt);
    if (!s_ctx) { JS_FreeRuntime(s_rt); s_rt = nullptr; vbjs_game::log("[js] failed to create context"); return; }

    vbjs_setup_context(s_ctx);
    // The browser inspector is opt-in via -dt, but VS Code can attach to any
    // running JS/TS mod. Keep its localhost-only bridge available either way.
    pthread_mutex_lock(&s_debugTransport.mutex);
    s_debugTransport.closed = 0; s_debugTransport.inbound.clear(); s_debugTransport.outbound[0].clear(); s_debugTransport.outbound[1].clear();
    pthread_mutex_unlock(&s_debugTransport.mutex);
    js_debugger_attach(s_ctx, vbjs_debug_read, vbjs_debug_write, vbjs_debug_peek, vbjs_debug_close, &s_debugTransport);
    VBJS_DevtoolsStartDebuggerBridge();
    // VS Code has to attach and submit breakpoints after the runtime starts.
    // Hold both top-level module evaluation and the one-shot `start` callback
    // until then, so breakpoints work everywhere in a freshly launched mod.
    s_scriptsLoaded = 0;
    s_startPending = 1;
    s_startWaitFrames = 0;
}

static void vbjs_load_scripts(void)
{
    for (int i = 0; i < s_scriptCount; i++)
    {
        const char *path = s_scripts[i].path;
        // require() resolves relative to the script currently being loaded.
        snprintf(s_scriptDir, sizeof(s_scriptDir), "%s", path);
        char *slash = strrchr(s_scriptDir, '/');
        if (slash) *slash = 0; else s_scriptDir[0] = 0;

        size_t len = 0; char *src = vbjs_load_source(path, &len);
        if (!src) { char m[1100]; snprintf(m, sizeof(m), "could not read %s", path); vbjs_game::log(m); continue; }

        JSValue r = JS_Eval(s_ctx, src, len, path, JS_EVAL_TYPE_MODULE);   // module so import/export work
        free(src);
        if (JS_IsException(r)) vbjs_dump_error(s_ctx);
        JS_FreeValue(s_ctx, r);
        { char m[1100]; snprintf(m, sizeof(m), "loaded %s", path); vbjs_game::log(m); }
    }
    s_scriptsLoaded = 1;
    s_enabled = 1;
}

void VBJS_Shutdown(void)
{
    if (!s_ctx) return;
    VBJS_FireSimple("shutdown");
#ifdef __APPLE__
    extern void VBJS_CocoaDevtoolsShutdown(void);
    VBJS_CocoaDevtoolsShutdown();
#endif
    VBJS_DevtoolsShutdownWindow();
    JS_FreeValue(s_ctx, s_events);
    JS_FreeValue(s_ctx, s_requireCache);
    JS_FreeValue(s_ctx, s_devtoolsEvent); s_devtoolsEvent = JS_UNDEFINED;
    JS_FreeContext(s_ctx); JS_FreeRuntime(s_rt);
    s_ctx = nullptr; s_rt = nullptr; s_enabled = 0; s_scriptsLoaded = 0; s_startPending = 0;
}

void VBJS_RunFrame(void)
{
    if (!s_ctx)
    {
        if (s_initTried) return;
        VBJS_Init();          // lazy: first frame is a safe point (game/engine are up)
        if (!s_ctx) return;   // no scripts, or init failed; VBJS_Init set s_initTried so we won't retry
    }
    VBJS_DevtoolsPump();
    if (s_startPending)
    {
        // Normal launches do not have a debugger client, so preserve startup
        // behavior after a brief bounded wait (~0.5 seconds at 60 fps).
        if (!js_debugger_configuration_done(s_ctx) && ++s_startWaitFrames < 30)
            return;
        s_startPending = 0;
        vbjs_load_scripts();
        VBJS_FireSimple("start");
    }
    if (s_devtoolsPaused) return;
    VBJS_FireSimple("frame");
    // pump promise jobs / microtasks
    JSContext *c;
    while (JS_ExecutePendingJob(s_rt, &c) > 0) {}
}

void VBJS_FireSimple(const char *event)
{
    if (!s_ctx) return;
    vbjs_fire(event, 0, nullptr);
}

void VBJS_FireActor(const char *event, int spriteNum, int owner, int tile)
{
    if (!s_ctx) return;
    JSValue o = JS_NewObject(s_ctx);
    JS_SetPropertyStr(s_ctx, o, "sprite", JS_NewInt32(s_ctx, spriteNum));
    JS_SetPropertyStr(s_ctx, o, "owner",  JS_NewInt32(s_ctx, owner));
    JS_SetPropertyStr(s_ctx, o, "tile",   JS_NewInt32(s_ctx, tile));   // picnum, for filtering
    JSValueConst args[1] = { o };
    vbjs_fire(event, 1, args);
    JS_FreeValue(s_ctx, o);
}

void VBJS_FireChat(const char *event, int playerNum, const char *text)
{
    if (!s_ctx) return;
    JSValue o = JS_NewObject(s_ctx);
    JS_SetPropertyStr(s_ctx, o, "player", JS_NewInt32(s_ctx, playerNum));
    JS_SetPropertyStr(s_ctx, o, "text",   JS_NewString(s_ctx, text ? text : ""));
    JSValueConst args[1] = { o };
    vbjs_fire(event, 1, args);
    JS_FreeValue(s_ctx, o);
}

void VBJS_FireConEvent(int eventNum, int spriteNum, int playerNum, int dist)
{
    if (!s_ctx || (unsigned)eventNum >= VBJS_MAXCONEVENTS || !s_conEventListened[eventNum]) return;
    const char *name = vbjs_game::eventName(eventNum);
    if (!name) return;
    JSValue o = JS_NewObject(s_ctx);
    JS_SetPropertyStr(s_ctx, o, "sprite", JS_NewInt32(s_ctx, spriteNum));
    JS_SetPropertyStr(s_ctx, o, "player", JS_NewInt32(s_ctx, playerNum));
    JS_SetPropertyStr(s_ctx, o, "dist",   JS_NewInt32(s_ctx, dist));
    JSValueConst args[1] = { o };
    vbjs_fire(name, 1, args);
    JS_FreeValue(s_ctx, o);
}

// VBJS_DrawHUD() and the vbjs_game::* bindings are implemented in vbjs_duke.cpp
// (they need the Duke game headers, which conflict with quickjs.h in one TU).

// Expose the HUD text store to the game-side draw code.
vbjs_text_t *VBJS_TextSlots(int *count) { if (count) *count = VBJS_MAX_TEXT; return s_text; }

} // extern "C"
