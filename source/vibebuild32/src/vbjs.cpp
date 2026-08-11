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

extern "C" {
#include "quickjs.h"
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
}

// ------------------------------------------------------------------------------

static JSRuntime *s_rt  = nullptr;
static JSContext *s_ctx = nullptr;
static char  s_scriptPath[1024] = {0};
static char  s_scriptDir[1024]  = {0};
static int   s_enabled = 0;

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
    if (msg) JS_FreeCString(ctx, msg);
    JSValue stack = JS_GetPropertyStr(ctx, exc, "stack");
    if (!JS_IsUndefined(stack))
    {
        const char *st = JS_ToCString(ctx, stack);
        if (st) { vbjs_game::log(st); JS_FreeCString(ctx, st); }
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
    JSValue arr = JS_GetPropertyStr(ctx, s_events, ev);
    if (!JS_IsArray(arr))
    {
        JS_FreeValue(ctx, arr);
        arr = JS_NewArray(ctx);
        JS_SetPropertyStr(ctx, s_events, ev, JS_DupValue(ctx, arr));
    }
    JSValue lenv = JS_GetPropertyStr(ctx, arr, "length");
    uint32_t len = 0; JS_ToUint32(ctx, &len, lenv); JS_FreeValue(ctx, lenv);
    JS_SetPropertyUint32(ctx, arr, len, JS_DupValue(ctx, argv[1]));
    JS_FreeValue(ctx, arr);
    JS_FreeCString(ctx, ev);
    return JS_UNDEFINED;
}

static JSValue js_vb_off(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    if (argc < 1) return JS_UNDEFINED;
    const char *ev = JS_ToCString(ctx, argv[0]);
    if (ev) { JS_SetPropertyStr(ctx, s_events, ev, JS_NewArray(ctx)); JS_FreeCString(ctx, ev); }
    return JS_UNDEFINED;
}

// Fire an event to every registered handler with the given args.
static void vbjs_fire(const char *event, int argc, JSValueConst *argv)
{
    if (!s_ctx) return;
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
    JS_SetPropertyStr(ctx, target, "spawn",       JS_NewCFunction(ctx, js_vb_spawn,       "spawn", 4));
    JS_SetPropertyStr(ctx, target, "sendMessage", JS_NewCFunction(ctx, js_vb_sendMessage, "sendMessage", 1));
    JS_SetPropertyStr(ctx, target, "getGamevar",  JS_NewCFunction(ctx, js_vb_getGamevar,  "getGamevar", 1));
    JS_SetPropertyStr(ctx, target, "setGamevar",  JS_NewCFunction(ctx, js_vb_setGamevar,  "setGamevar", 2));
    JS_SetPropertyStr(ctx, target, "player",      JS_NewCFunction(ctx, js_vb_player,      "player", 0));
    JS_SetPropertyStr(ctx, target, "run",         JS_NewCFunction(ctx, js_cvar_run,       "run", 1));   // alias of cvar.run
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
static const char *k_vb_exports[] = { "on","off","log","drawText","clearText","spawn","sendMessage","getGamevar","setGamevar","player" };
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

    size_t len = 0; char *src = vbjs_read_file(path, &len);
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
    size_t len = 0; char *buf = vbjs_read_file(module_name, &len);
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

    // global require + a global `vb` object (so scripts work without imports too)
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

void VBJS_SetScriptPath(const char *path)
{
    if (!path) return;
    snprintf(s_scriptPath, sizeof(s_scriptPath), "%s", path);
    snprintf(s_scriptDir, sizeof(s_scriptDir), "%s", path);
    char *slash = strrchr(s_scriptDir, '/');
    if (slash) *slash = 0; else s_scriptDir[0] = 0;
}

void VBJS_Init(void)
{
    if (s_scriptPath[0] == 0 || s_ctx) return;
    s_rt = JS_NewRuntime();
    if (!s_rt) { vbjs_game::log("[js] failed to create runtime"); return; }
    s_ctx = JS_NewContext(s_rt);
    if (!s_ctx) { JS_FreeRuntime(s_rt); s_rt = nullptr; vbjs_game::log("[js] failed to create context"); return; }

    vbjs_setup_context(s_ctx);

    size_t len = 0; char *src = vbjs_read_file(s_scriptPath, &len);
    if (!src) { vbjs_game::log("[js] could not read script"); return; }

    // Run the main file as a module so import/export work.
    JSValue r = JS_Eval(s_ctx, src, len, s_scriptPath, JS_EVAL_TYPE_MODULE);
    free(src);
    if (JS_IsException(r)) vbjs_dump_error(s_ctx);
    JS_FreeValue(s_ctx, r);

    s_enabled = 1;
    { char m[1100]; snprintf(m, sizeof(m), "loaded %s", s_scriptPath); vbjs_game::log(m); }
    VBJS_FireSimple("start");
}

void VBJS_Shutdown(void)
{
    if (!s_ctx) return;
    VBJS_FireSimple("shutdown");
    JS_FreeValue(s_ctx, s_events);
    JS_FreeValue(s_ctx, s_requireCache);
    JS_FreeContext(s_ctx); JS_FreeRuntime(s_rt);
    s_ctx = nullptr; s_rt = nullptr; s_enabled = 0;
}

void VBJS_RunFrame(void)
{
    if (!s_ctx)
    {
        if (s_scriptPath[0] == 0) return;
        VBJS_Init();          // lazy: first frame is a safe point (game/engine are up)
        if (!s_ctx) { s_scriptPath[0] = 0; return; }   // init failed; don't retry every frame
    }
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

// VBJS_DrawHUD() and the vbjs_game::* bindings are implemented in vbjs_duke.cpp
// (they need the Duke game headers, which conflict with quickjs.h in one TU).

// Expose the HUD text store to the game-side draw code.
vbjs_text_t *VBJS_TextSlots(int *count) { if (count) *count = VBJS_MAX_TEXT; return s_text; }

} // extern "C"
