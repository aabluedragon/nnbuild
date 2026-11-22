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
#include <algorithm>
#include <chrono>
#include <map>
#include <set>
#include <string>
#include <vector>

extern "C" {
#include "quickjs.h"
}
#include "vbjs.h"
#include "editorproject.h"
#include "sjson.h"

// ---- Duke game hooks (implemented at the bottom, kept together for porting) ----
namespace vbjs_game {
    void log(const char *s);                                  // console output into the game log
    int  spawn(int tile, int x, int y, int z, int owner);     // -> spriteNum, or -1
    void sendChat(const char *text);                          // broadcast a multiplayer/console message
    bool isServer(void);
    void sendPrivate(int player, uint32_t connection, const char *text);
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
struct ScriptComponent
{
    JSValue context = JS_UNDEFINED;
    JSValue update = JS_UNDEFINED, fixedUpdate = JS_UNDEFINED;
    JSValue detach = JS_UNDEFINED, dispose = JS_UNDEFINED;
    int sprite = -1;
    bool removed = false;
};

static std::vector<ScriptComponent> s_components;
static std::set<int> s_deletedDuringLoad;
static bool s_loadingLevel;
static unsigned s_componentEpoch;
static std::chrono::steady_clock::time_point s_lastComponentFrame;
static constexpr char componentSuffix[] = "?vbcomponent=";
static char  s_scriptDir[1024]  = {0};   // dir of the script currently being loaded (require base)
static int   s_enabled = 0;
static int   s_initTried = 0;            // guards a single init attempt
static int   s_devtools = 0;
enum { VBJS_DEVTOOLS_LOGS = 12 };
static char s_devtoolsLogs[VBJS_DEVTOOLS_LOGS][192];
static int s_devtoolsLogCount = 0;
static char s_devtoolsBreakpointEvents[16][64];
static int s_devtoolsBreakpointCount = 0;
static int s_devtoolsPaused = 0;
static char s_devtoolsCurrentEvent[64];
static JSValue s_devtoolsEvent = JS_UNDEFINED;

static void vbjs_devtools_log(const char *line)
{
    if (!s_devtools) return;
    snprintf(s_devtoolsLogs[s_devtoolsLogCount % VBJS_DEVTOOLS_LOGS], sizeof(s_devtoolsLogs[0]), "%s", line ? line : "");
    ++s_devtoolsLogCount;
}

static int vbjs_devtools_has_breakpoint(const char *event)
{
    for (int i = 0; i < s_devtoolsBreakpointCount; ++i)
        if (!strcmp(s_devtoolsBreakpointEvents[i], event)) return 1;
    return 0;
}

static void vbjs_devtools_set_event(const char *event, int argc, JSValueConst *argv)
{
    if (!s_devtools || !vbjs_devtools_has_breakpoint(event)) return;
    snprintf(s_devtoolsCurrentEvent, sizeof(s_devtoolsCurrentEvent), "%s", event);
    JS_FreeValue(s_ctx, s_devtoolsEvent);
    s_devtoolsEvent = argc ? JS_DupValue(s_ctx, argv[0]) : JS_UNDEFINED;
    s_devtoolsPaused = 1; // cooperative pause: no subsequent JS events/jobs run until Resume.
}

// The set of scripts to load: -js files plus autojs/<name>/index.{ts,js}.
struct vbjs_script_t { char path[1024]; };
enum { VBJS_MAX_SCRIPTS = 64 };
static vbjs_script_t s_scripts[VBJS_MAX_SCRIPTS];
static int s_scriptCount = 0;
static char s_launchDir[1024];   // captured at app_main() before any chdir; holds autojs/

static void vbjs_add_script(const char *path)
{
    if (!path || s_scriptCount >= VBJS_MAX_SCRIPTS) return;
    for (int i = 0; i < s_scriptCount; i++)
        if (!strcmp(s_scripts[i].path, path)) return;   // dedupe
    snprintf(s_scripts[s_scriptCount++].path, sizeof(s_scripts[0].path), "%s", path);
}

static JSValue s_events;   // { eventName: [fn, ...] }
static JSValue s_serverCommands;

using Clock = std::chrono::steady_clock;
struct ScriptTimer
{
    Clock::time_point due;
    std::chrono::milliseconds delay;
    bool repeats;
    std::vector<JSValue> values;
};
static std::map<uint32_t, ScriptTimer> s_timers;
static uint32_t s_nextTimer;

static void vbjs_clear_timer(JSContext *ctx, uint32_t id)
{
    auto const timer = s_timers.find(id);
    if (timer == s_timers.end())
        return;
    for (auto value : timer->second.values)
        JS_FreeValue(ctx, value);
    s_timers.erase(timer);
}

static JSValue js_set_timer(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv, int repeats)
{
    if (argc < 1 || !JS_IsFunction(ctx, argv[0]))
        return JS_ThrowTypeError(ctx, "Expected a timer callback");

    int64_t milliseconds = 0;
    if (argc >= 2 && JS_ToInt64(ctx, &milliseconds, argv[1]) < 0)
        return JS_EXCEPTION;
    auto const delay = std::chrono::milliseconds(std::min<int64_t>(INT32_MAX, std::max<int64_t>(1, milliseconds)));
    ScriptTimer timer = {Clock::now() + delay, delay, repeats != 0, {JS_DupValue(ctx, argv[0])}};
    for (int i = 2; i < argc; ++i)
        timer.values.push_back(JS_DupValue(ctx, argv[i]));

    auto const id = ++s_nextTimer;
    s_timers.emplace(id, std::move(timer));
    return JS_NewUint32(ctx, id);
}

static JSValue js_clear_timer(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    uint32_t id;
    if (argc > 0)
    {
        if (JS_ToUint32(ctx, &id, argv[0]) < 0)
            return JS_EXCEPTION;
        vbjs_clear_timer(ctx, id);
    }
    return JS_UNDEFINED;
}

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

static void vbjs_pump_timers(JSContext *ctx)
{
    if (s_timers.empty())
        return;

    auto const now = Clock::now();
    std::vector<uint32_t> due;
    for (auto const &timer : s_timers)
        if (timer.second.due <= now)
            due.push_back(timer.first);

    for (auto const id : due)
    {
        auto const timer = s_timers.find(id);
        if (timer == s_timers.end())
            continue;
        std::vector<JSValue> values;
        for (auto value : timer->second.values)
            values.push_back(JS_DupValue(ctx, value));
        if (timer->second.repeats)
            timer->second.due = now + timer->second.delay;
        else
            vbjs_clear_timer(ctx, id);
        JSValue result = JS_Call(ctx, values[0], JS_UNDEFINED, values.size() - 1, values.data() + 1);
        if (JS_IsException(result))
            vbjs_dump_error(ctx);
        JS_FreeValue(ctx, result);
        for (auto value : values)
            JS_FreeValue(ctx, value);
    }
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
    int const conEv = vbjs_game::eventNum(ev);
    char const *key = conEv >= 0 ? vbjs_game::eventName(conEv) : ev;
    JSValue arr = JS_GetPropertyStr(ctx, s_events, key);
    if (!JS_IsArray(arr))
    {
        JS_FreeValue(ctx, arr);
        arr = JS_NewArray(ctx);
        JS_SetPropertyStr(ctx, s_events, key, JS_DupValue(ctx, arr));
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
        JS_SetPropertyStr(ctx, s_events, conEv >= 0 ? vbjs_game::eventName(conEv) : ev, JS_NewArray(ctx));
        if (conEv >= 0 && conEv < VBJS_MAXCONEVENTS)
            s_conEventListened[conEv] = 0;
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
    vbjs_devtools_set_event(event, argc, argv);
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

static JSValue js_vb_sendMessage(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    if (argc < 1) return JS_UNDEFINED;
    const char *s = JS_ToCString(ctx, argv[0]);
    if (s) { vbjs_game::sendChat(s); JS_FreeCString(ctx, s); }
    return JS_UNDEFINED;
}

static std::string commandName(char const *text)
{
    std::string name;
    if (*text == '!')
        ++text;
    for (; *text; ++text)
    {
        unsigned char const c = *text;
        if ((!isalnum(c) && c != '_' && c != '-') || c >= 128 || name.size() >= 32)
            return {};
        name.push_back(static_cast<char>(tolower(c)));
    }
    return name;
}

static JSValue js_vb_registerCommand(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    if (argc < 2 || !JS_IsFunction(ctx, argv[1]))
        return JS_ThrowTypeError(ctx, "vb.registerCommand(name, handler)");
    if (!vbjs_game::isServer())
        return JS_ThrowTypeError(ctx, "Player commands must be registered on the server");

    char const *text = JS_ToCString(ctx, argv[0]);
    if (!text)
        return JS_EXCEPTION;
    auto const name = commandName(text);
    JS_FreeCString(ctx, text);
    if (name.empty())
        return JS_ThrowTypeError(ctx, "Invalid server command name");

    if (JS_SetPropertyStr(ctx, s_serverCommands, name.c_str(), JS_DupValue(ctx, argv[1])) < 0)
        return JS_EXCEPTION;
    return JS_UNDEFINED;
}

static JSValue js_server_reply(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv,
                               int, JSValueConst *data)
{
    if (argc < 1)
        return JS_UNDEFINED;
    int32_t player;
    uint32_t connection;
    if (JS_ToInt32(ctx, &player, data[0]) < 0 || JS_ToUint32(ctx, &connection, data[1]) < 0)
        return JS_EXCEPTION;
    char const *text = JS_ToCString(ctx, argv[0]);
    if (!text)
        return JS_EXCEPTION;
    vbjs_game::sendPrivate(player, connection, text);
    JS_FreeCString(ctx, text);
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
    JS_SetPropertyStr(ctx, target, "sendMessage", JS_NewCFunction(ctx, js_vb_sendMessage, "sendMessage", 1));
    JS_SetPropertyStr(ctx, target, "registerCommand", JS_NewCFunction(ctx, js_vb_registerCommand, "registerCommand", 2));
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
    "on","off","log","drawText","clearText","breakpoint","spawn","sendMessage","registerCommand","getGamevar","setGamevar","player","run","define",
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
    // Files imported by components are shared within a level and reloaded on the next playtest.
    std::string importName(name);
    std::string baseName = base ? base : "";
    auto const epoch = baseName.find(componentSuffix);
    if (epoch != std::string::npos)
    {
        if (importName.find(componentSuffix) == std::string::npos)
            importName += baseName.substr(epoch);
        baseName.resize(epoch);
        name = importName.c_str();
    }
    // Relative to the importing module's directory when possible, else the script dir.
    if (name[0] == '/' || (name[0] && name[1] == ':'))
        return js_strdup(ctx, name);
    auto const slash = baseName.rfind('/');
    if (slash != std::string::npos)
        return js_strdup(ctx, (baseName.substr(0, slash + 1) + name).c_str());
    char resolved[1024];
    vbjs_resolve(name, resolved, sizeof(resolved));
    return js_strdup(ctx, resolved);
}

static JSModuleDef *vbjs_module_loader(JSContext *ctx, const char *module_name, void *)
{
    std::string path(module_name);
    auto const epoch = path.find(componentSuffix);
    if (epoch != std::string::npos)
        path.resize(epoch);
    size_t len = 0; char *buf = vbjs_load_source(path.c_str(), &len);
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
    s_events       = JS_NewObjectProto(ctx, JS_NULL);
    s_requireCache = JS_NewObjectProto(ctx, JS_NULL);
    s_serverCommands = JS_NewObjectProto(ctx, JS_NULL);

    JSValue g = JS_GetGlobalObject(ctx);

    // console
    JSValue console = JS_NewObject(ctx);
    JSValue logfn = JS_NewCFunction(ctx, js_console_log, "log", 1);
    JS_SetPropertyStr(ctx, console, "log",   JS_DupValue(ctx, logfn));
    JS_SetPropertyStr(ctx, console, "error", JS_DupValue(ctx, logfn));
    JS_SetPropertyStr(ctx, console, "warn",  JS_DupValue(ctx, logfn));
    JS_SetPropertyStr(ctx, console, "info",  logfn);
    JS_SetPropertyStr(ctx, g, "console", console);
    JS_SetPropertyStr(ctx, g, "setTimeout", JS_NewCFunctionMagic(ctx, js_set_timer, "setTimeout", 2, JS_CFUNC_generic_magic, 0));
    JS_SetPropertyStr(ctx, g, "setInterval", JS_NewCFunctionMagic(ctx, js_set_timer, "setInterval", 2, JS_CFUNC_generic_magic, 1));
    JS_SetPropertyStr(ctx, g, "clearTimeout", JS_NewCFunction(ctx, js_clear_timer, "clearTimeout", 1));
    JS_SetPropertyStr(ctx, g, "clearInterval", JS_NewCFunction(ctx, js_clear_timer, "clearInterval", 1));

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

void VBJS_SetScriptPath(const char *path)
{
    std::string source = path;
    std::replace(source.begin(), source.end(), '\\', '/');
    if (!source.empty() && source[0] != '/' && (source.size() < 2 || source[1] != ':') && s_launchDir[0])
        source = std::string(s_launchDir) + "/" + source;
    std::replace(source.begin(), source.end(), '\\', '/');
    vbjs_add_script(source.c_str());
}

void VBJS_SetDevtools(int enabled) { s_devtools = enabled != 0; }
int VBJS_DevtoolsEnabled(void) { return s_devtools; }
const char *VBJS_DevtoolsLog(int index)
{
    if (index < 0 || index >= VBJS_DEVTOOLS_LOGS || index >= s_devtoolsLogCount) return nullptr;
    int first = s_devtoolsLogCount > VBJS_DEVTOOLS_LOGS ? s_devtoolsLogCount - VBJS_DEVTOOLS_LOGS : 0;
    if (index + first >= s_devtoolsLogCount) return nullptr;
    return s_devtoolsLogs[(first + index) % VBJS_DEVTOOLS_LOGS];
}
int VBJS_DevtoolsPaused(void) { return s_devtoolsPaused; }
void VBJS_DevtoolsResume(void) { s_devtoolsPaused = 0; }
void VBJS_DevtoolsSetEventBreakpoint(const char *event, int enabled)
{
    if (!event || !event[0]) return;
    for (int i = 0; i < s_devtoolsBreakpointCount; ++i)
        if (!strcmp(s_devtoolsBreakpointEvents[i], event))
        {
            if (enabled) return;
            memmove(&s_devtoolsBreakpointEvents[i], &s_devtoolsBreakpointEvents[i + 1],
                    (size_t)(s_devtoolsBreakpointCount - i - 1) * sizeof(s_devtoolsBreakpointEvents[0]));
            --s_devtoolsBreakpointCount;
            return;
        }
    if (enabled && s_devtoolsBreakpointCount < (int)(sizeof(s_devtoolsBreakpointEvents) / sizeof(s_devtoolsBreakpointEvents[0])))
        snprintf(s_devtoolsBreakpointEvents[s_devtoolsBreakpointCount++], sizeof(s_devtoolsBreakpointEvents[0]), "%s", event);
}
int VBJS_DevtoolsHasEventBreakpoint(const char *event) { return event && vbjs_devtools_has_breakpoint(event); }
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
    char cwd[1024];
    if (DukeEditor::ProjectAllowsAutoScripts() && getcwd(cwd, sizeof(cwd)) && strcmp(cwd, s_launchDir))
        vbjs_scan_autojs_in(cwd);
}

static bool vbjs_create_context()
{
    s_rt = JS_NewRuntime();
    if (!s_rt) { vbjs_game::log("[js] failed to create runtime"); return false; }
    s_ctx = JS_NewContext(s_rt);
    if (!s_ctx) { JS_FreeRuntime(s_rt); s_rt = nullptr; vbjs_game::log("[js] failed to create context"); return false; }
    vbjs_setup_context(s_ctx);
    return true;
}

void VBJS_Init(void)
{
    if (s_ctx || s_initTried) return;
    s_initTried = 1;
    vbjs_scan_autojs();
    if (s_scriptCount == 0 || !vbjs_create_context())
        return;

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

    s_enabled = 1;
    VBJS_FireSimple("start");
}

void VBJS_BeginLevelAttachments(void)
{
    VBJS_ClearAttachments();
    s_loadingLevel = true;
}

void VBJS_EndLevelAttachments(void) { s_loadingLevel = false; }

static JSValue componentFunction(JSValueConst module, const char *name)
{
    auto const fn = JS_GetPropertyStr(s_ctx, module, name);
    if (JS_IsFunction(s_ctx, fn))
        return fn;
    JS_FreeValue(s_ctx, fn);
    return JS_UNDEFINED;
}

void VBJS_ActivateAttachment(const char *script, const char *map, const char *kind, int index)
{
    auto const spriteIndex = strcmp(kind, "sprite") == 0 ? index : -1;
    if (spriteIndex >= 0 && s_deletedDuringLoad.count(spriteIndex))
    {
        vbjs_game::log(("Component skipped for a sprite removed during map setup: " + std::to_string(index)).c_str());
        return;
    }
    if (!s_ctx)
    {
        if (!vbjs_create_context())
            return;
        s_initTried = s_enabled = 1;
    }
    auto const json = sjson_create_context(0, 0, nullptr);
    auto const path = std::string(script) + componentSuffix + std::to_string(s_componentEpoch);
    auto const quoted = sjson_stringify(json, sjson_mkstring(json, path.c_str()), nullptr);
    auto const body = std::string("export * as component from ") + quoted + ";";
    sjson_free_string(json, quoted);
    sjson_destroy_context(json);
    static unsigned sequence;
    auto const name = "<map-object-" + std::to_string(++sequence) + ">";
    auto const compiled = JS_Eval(s_ctx, body.c_str(), body.size(), name.c_str(), JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(compiled))
    {
        vbjs_dump_error(s_ctx);
        return;
    }
    auto const module = static_cast<JSModuleDef *>(JS_VALUE_GET_PTR(compiled));
    auto const result = JS_EvalFunction(s_ctx, compiled);
    if (JS_IsException(result) || JS_PromiseState(s_ctx, result) == JS_PROMISE_REJECTED)
    {
        if (!JS_IsException(result))
            JS_Throw(s_ctx, JS_PromiseResult(s_ctx, result));
        vbjs_dump_error(s_ctx);
        JS_FreeValue(s_ctx, result);
        return;
    }
    if (JS_PromiseState(s_ctx, result) != JS_PROMISE_FULFILLED)
    {
        vbjs_game::log((std::string("Component module must initialize synchronously: ") + script).c_str());
        JS_FreeValue(s_ctx, result);
        return;
    }
    auto const exports = JS_GetModuleNamespace(s_ctx, module);
    JS_FreeValue(s_ctx, result);
    auto const component = JS_GetPropertyStr(s_ctx, exports, "component");
    JS_FreeValue(s_ctx, exports);

    ScriptComponent instance;
    instance.sprite = spriteIndex;
    instance.context = JS_NewObject(s_ctx);
    JS_SetPropertyStr(s_ctx, instance.context, "kind", JS_NewString(s_ctx, kind));
    JS_SetPropertyStr(s_ctx, instance.context, "index", JS_NewInt32(s_ctx, index));
    JS_SetPropertyStr(s_ctx, instance.context, "map", JS_NewString(s_ctx, map));
    JS_SetPropertyStr(s_ctx, instance.context, "state", JS_NewObject(s_ctx));
    auto const attach = componentFunction(component, "onAttach");
    if (!JS_IsUndefined(attach))
    {
        auto const cleanup = JS_Call(s_ctx, attach, JS_UNDEFINED, 1, &instance.context);
        JS_FreeValue(s_ctx, attach);
        if (JS_IsException(cleanup))
        {
            vbjs_dump_error(s_ctx);
            JS_FreeValue(s_ctx, instance.context);
            JS_FreeValue(s_ctx, component);
            return;
        }
        if (JS_IsFunction(s_ctx, cleanup))
            instance.dispose = cleanup;
        else
            JS_FreeValue(s_ctx, cleanup);
    }
    instance.update = componentFunction(component, "onUpdate");
    instance.fixedUpdate = componentFunction(component, "onFixedUpdate");
    instance.detach = componentFunction(component, "onDetach");
    JS_FreeValue(s_ctx, component);
    s_components.push_back(instance);
}

static void disposeComponent(ScriptComponent &instance)
{
    auto call = [](JSValueConst fn, int argc, JSValueConst *args) {
        if (JS_IsUndefined(fn))
            return;
        auto const result = JS_Call(s_ctx, fn, JS_UNDEFINED, argc, args);
        if (JS_IsException(result))
            vbjs_dump_error(s_ctx);
        JS_FreeValue(s_ctx, result);
    };
    call(instance.dispose, 0, nullptr);
    call(instance.detach, 1, &instance.context);
    for (auto const value : {instance.context, instance.update, instance.fixedUpdate, instance.detach, instance.dispose})
        JS_FreeValue(s_ctx, value);
}

void VBJS_ClearAttachments(void)
{
    auto components = std::move(s_components);
    s_components.clear();
    for (auto it = components.rbegin(); it != components.rend(); ++it)
        disposeComponent(*it);
    s_deletedDuringLoad.clear();
    s_loadingLevel = false;
    s_lastComponentFrame = {};
    ++s_componentEpoch;
}

void VBJS_NotifySpriteDeleted(int spriteNum)
{
    if (s_loadingLevel)
        s_deletedDuringLoad.insert(spriteNum);
    for (auto &instance : s_components)
        if (instance.sprite == spriteNum)
            instance.removed = true;
}

static void updateComponents(JSValue ScriptComponent::*hook, double deltaSeconds)
{
    auto const delta = JS_NewFloat64(s_ctx, deltaSeconds);
    for (auto &instance : s_components)
    {
        auto &fn = instance.*hook;
        if (instance.removed || JS_IsUndefined(fn))
            continue;
        JSValueConst args[] = {instance.context, delta};
        auto const result = JS_Call(s_ctx, fn, JS_UNDEFINED, 2, args);
        if (JS_IsException(result))
        {
            vbjs_dump_error(s_ctx);
            JS_FreeValue(s_ctx, fn);
            fn = JS_UNDEFINED;
        }
        JS_FreeValue(s_ctx, result);
    }
    JS_FreeValue(s_ctx, delta);
    auto const removed = std::remove_if(s_components.begin(), s_components.end(), [](ScriptComponent &instance) {
        if (!instance.removed) return false;
        disposeComponent(instance);
        return true;
    });
    s_components.erase(removed, s_components.end());
}

void VBJS_RunFixedUpdate(double deltaSeconds)
{
    if (!s_components.empty() && !s_devtoolsPaused)
        updateComponents(&ScriptComponent::fixedUpdate, deltaSeconds);
}

void VBJS_Shutdown(void)
{
    if (!s_ctx) return;
    VBJS_ClearAttachments();
    VBJS_FireSimple("shutdown");
    while (!s_timers.empty())
        vbjs_clear_timer(s_ctx, s_timers.begin()->first);
    JS_FreeValue(s_ctx, s_serverCommands);
    JS_FreeValue(s_ctx, s_events);
    JS_FreeValue(s_ctx, s_requireCache);
    JS_FreeValue(s_ctx, s_devtoolsEvent); s_devtoolsEvent = JS_UNDEFINED;
    JS_FreeContext(s_ctx); JS_FreeRuntime(s_rt);
    s_ctx = nullptr; s_rt = nullptr; s_enabled = 0;
}

void VBJS_RunFrame(int activeLevel)
{
    if (!s_ctx)
    {
        if (s_initTried) return;
        VBJS_Init();          // lazy: first frame is a safe point (game/engine are up)
        if (!s_ctx) return;   // no scripts, or init failed; VBJS_Init set s_initTried so we won't retry
    }
    if (!activeLevel || s_devtoolsPaused)
        s_lastComponentFrame = {};
    if (s_devtoolsPaused) return;
    if (activeLevel && !s_components.empty())
    {
        auto const now = Clock::now();
        auto const dt = s_lastComponentFrame == Clock::time_point{} ? 0. :
            std::min(.25, std::chrono::duration<double>(now - s_lastComponentFrame).count());
        s_lastComponentFrame = now;
        updateComponents(&ScriptComponent::update, dt);
    }
    VBJS_FireSimple("frame");
    vbjs_pump_timers(s_ctx);
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

int VBJS_RunServerCommand(int player, uint32_t connection, const char *text)
{
    if (!s_ctx || !vbjs_game::isServer())
        return 0;

    auto const length = strcspn(text, " \t");
    auto const name = commandName(std::string(text, length).c_str());
    if (name.empty())
        return 0;
    JSValue handler = JS_GetPropertyStr(s_ctx, s_serverCommands, name.c_str());
    if (!JS_IsFunction(s_ctx, handler))
    {
        JS_FreeValue(s_ctx, handler);
        return 0;
    }

    char const *args = text + length;
    args += strspn(args, " \t");
    JSValue binding[] = {JS_NewInt32(s_ctx, player), JS_NewUint32(s_ctx, connection)};
    JSValue request = JS_NewObject(s_ctx);
    JS_SetPropertyStr(s_ctx, request, "player", binding[0]);
    JS_SetPropertyStr(s_ctx, request, "command", JS_NewString(s_ctx, name.c_str()));
    JS_SetPropertyStr(s_ctx, request, "args", JS_NewString(s_ctx, args));
    JS_SetPropertyStr(s_ctx, request, "reply", JS_NewCFunctionData(s_ctx, js_server_reply, 1, 0, 2, binding));
    JSValue result = JS_Call(s_ctx, handler, JS_UNDEFINED, 1, &request);
    if (JS_IsException(result))
        vbjs_dump_error(s_ctx);
    else if (JS_IsString(result))
        js_server_reply(s_ctx, JS_UNDEFINED, 1, &result, 0, binding);
    JS_FreeValue(s_ctx, result);
    JS_FreeValue(s_ctx, request);
    JS_FreeValue(s_ctx, handler);
    return 1;
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
