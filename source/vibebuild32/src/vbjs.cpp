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
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <atomic>
#include <deque>
#include <vector>
#include <unordered_map>
#include <string>
#include <curl/curl.h>

#ifdef _WIN32
# include <direct.h>
#endif

extern "C" {
#include "quickjs.h"
#include "quickjs-debugger.h"
}
#include "vbjs.h"
#include "baselayer.h"

// ---- Duke game hooks (implemented at the bottom, kept together for porting) ----
namespace vbjs_game {
    void log(const char *s);                                  // console output into the game log
    int  spawn(int tile, int x, int y, int z, int owner);     // -> spriteNum, or -1
    void sendChat(const char *text);                          // broadcast a multiplayer/console message
    int  getGamevar(const char *name, int spriteNum, int playerNum, int *ok);
    void setGamevar(const char *name, int value, int spriteNum, int playerNum);
    int  playerSprite(void);                                  // current view player's sprite, or -1
    int  gameStarted(void);
    int  skill(void), episode(void), level(void), localPlayer(void), hostPlayer(void), playerCount(void), playerAt(int ordinal), playerSpriteAt(int player);
    const char *episodeName(void), *levelName(void), *userMap(void), *playerName(int player), *connectedServer(void);
    void setMenuBackground(int tile);
    int currentMenu(void);
    void openMenu(int menu);
    int menuOpen(void);
    int menuSelection(void);
    void setMenuSelection(int selection);
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
static int   s_gameStartedPending = 0;
static int   s_gameSessionActive = 0;
static int   s_fatalScriptError = 0;
static int   s_genericMode = 0;
struct vbjs_fs_callback_t { JSValue callback, value; int error, existsStyle; };
static std::vector<vbjs_fs_callback_t> s_fsCallbacks;

// The shared runtime is also used by games that do not have Duke's logging
// globals.  Keep their script diagnostics on the process log instead of
// touching Duke's OSD state.
static void vbjs_log(const char *message)
{
    if (s_genericMode)
    {
        initprintf("[js] %s\n", message ? message : "");
    }
    else vbjs_game::log(message);
}

// UDP sockets are non-blocking and polled by VBJS_RunFrame.  That keeps all
// QuickJS calls on the game thread while preserving Node's event-driven dgram
// model.
struct vbjs_dgram_socket_t { int fd = -1; int family = AF_INET; JSValue listeners = JS_UNDEFINED; };
static JSClassID s_dgramSocketClass;
static std::vector<vbjs_dgram_socket_t *> s_dgramSockets;
struct vbjs_net_socket_t { int fd = -1; int connecting = 0; int closed = 0; JSValue listeners = JS_UNDEFINED; };
static JSClassID s_netSocketClass;
static std::vector<vbjs_net_socket_t *> s_netSockets;
struct vbjs_fetch_result_t { JSValue resolve, reject; long status = 0; std::string body, error, url; std::unordered_map<std::string, std::string> headers; };
static pthread_mutex_t s_fetchMutex = PTHREAD_MUTEX_INITIALIZER;
static std::deque<vbjs_fetch_result_t *> s_fetchReady;
static std::atomic<bool> s_fetchShuttingDown { false };
struct vbjs_fetch_request_t { char *url; char *method; std::vector<uint8_t> body; std::vector<std::string> headers; vbjs_fetch_result_t *result; };
static int   s_observedMenuOpen = -1;
static int   s_observedMenuId = -1;
// The game calls VBJS_RunFrame every rendered frame. Avoid crossing into
// QuickJS at all when no script has requested the high-frequency event.
static int   s_frameEventListened = 0;
static int   s_devtools = 0;
static int   s_debug = 0;
static int   s_debugPort = 8501;
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
// The debugger listener runs on its own thread.  Avoid taking the transport
// mutex in every game frame when neither the browser inspector nor VS Code has
// submitted a request.
static std::atomic<bool> s_debuggerWorkPending { false };
static std::unordered_map<std::string, std::string> s_menuTextOverrides;
static std::unordered_map<uint64_t, int> s_menuRoutes;

static uint64_t vbjs_menu_route_key(int menu, int item)
{
    return (uint64_t)(uint32_t)menu << 32 | (uint32_t)item;
}

extern "C" const char *VBJS_MenuText(const char *text)
{
    if (!text) return text;
    auto const it = s_menuTextOverrides.find(text);
    return it == s_menuTextOverrides.end() ? text : it->second.c_str();
}

extern "C" int VBJS_MenuRoute(int fromMenu, int itemIndex, int requestedMenu)
{
    auto it = s_menuRoutes.find(vbjs_menu_route_key(fromMenu, itemIndex));
    if (it == s_menuRoutes.end()) it = s_menuRoutes.find(vbjs_menu_route_key(fromMenu, -1));
    return it == s_menuRoutes.end() ? requestedMenu : it->second;
}

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
static char s_exeDir[1024];      // argv[0] directory; used by --gen-* template fallback.

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
    char canonical[2048];
    const char *storedPath = realpath(path, canonical) ? canonical : path;
    for (int i = 0; i < s_scriptCount; i++)
        if (!strcmp(s_scripts[i].path, storedPath)) return;   // dedupe
    snprintf(s_scripts[s_scriptCount++].path, sizeof(s_scripts[0].path), "%s", storedPath);
}

static JSValue s_events;   // { eventName: [fn, ...] }
// JS-defined component classes/functions shared by the global, CommonJS, and
// ES-module forms of `vb`.  They intentionally live in one closure so a
// component attached through any import style is visible everywhere.
static JSValue s_componentExports = JS_UNDEFINED;

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

static char *vbjs_read_vscode_template(const char *file, size_t *out_len)
{
    char source[1400];

    snprintf(source, sizeof(source), "%s/autojs/my-mod/.vscode/%s", s_launchDir, file);
    if (char *contents = vbjs_read_file(source, out_len)) return contents;

    if (s_exeDir[0])
    {
        snprintf(source, sizeof(source), "%s/autojs/my-mod/.vscode/%s", s_exeDir, file);
        if (char *contents = vbjs_read_file(source, out_len)) return contents;
    }

    return nullptr;
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

[[noreturn]] static void vbjs_fatal_error(const char *context, const char *message, const char *stack = nullptr)
{
    char summary[8192];
    snprintf(summary, sizeof(summary), "%s%s%s", context ? context : "JavaScript error", context ? ": " : "", message ? message : "(unknown JavaScript error)");
    vbjs_log(summary);
    vbjs_devtools_log(summary);
    if (stack && stack[0]) { vbjs_log(stack); vbjs_devtools_log(stack); }

    // Native message boxes on macOS provide selectable/copyable message text.
    // Show this before ending the process: errors otherwise vanish into the
    // engine log, especially for a TypeScript parse failure during startup.
    if (!s_fatalScriptError++)
    {
        char detail[12000];
        snprintf(detail, sizeof(detail), "%s%s%s\n\nThe full text has been copied to the clipboard and written to the VibeBuild32 log. The game will close after this dialog is dismissed.", summary,
                 stack && stack[0] ? "\n\n" : "", stack && stack[0] ? stack : "");
        SDL_SetClipboardText(detail);
        wm_msgbox("VibeBuild32 JavaScript Error", "%s", detail);
    }
    exit(EXIT_FAILURE);
}

static void vbjs_dump_error(JSContext *ctx, const char *context = nullptr)
{
    JSValue exc = JS_GetException(ctx);
    const char *msg = JS_ToCString(ctx, exc);
    char message[2048]; snprintf(message, sizeof(message), "%s", msg ? msg : "(unknown JS error)");
    if (msg) JS_FreeCString(ctx, msg);
    char stackText[6144] = {};
    JSValue stack = JS_GetPropertyStr(ctx, exc, "stack");
    if (!JS_IsUndefined(stack))
    {
        const char *st = JS_ToCString(ctx, stack);
        if (st) { snprintf(stackText, sizeof(stackText), "%s", st); JS_FreeCString(ctx, st); }
    }
    JS_FreeValue(ctx, stack);
    JS_FreeValue(ctx, exc);
    vbjs_fatal_error(context ? context : "JavaScript error", message, stackText);
}

// QuickJS reports a bare `Promise.reject()` through this runtime hook instead
// of JS_ExecutePendingJob().  Without it, an unhandled async failure vanishes.
static void vbjs_promise_rejection_tracker(JSContext *ctx, JSValueConst, JSValueConst reason, bool isHandled, void *)
{
    if (isHandled) return;
    const char *message = JS_ToCString(ctx, reason);
    char text[2048]; snprintf(text, sizeof(text), "%s", message ? message : "(unknown rejection)");
    if (message) JS_FreeCString(ctx, message);
    char stackText[6144] = {};

    if (JS_IsObject(reason))
    {
        JSValue stack = JS_GetPropertyStr(ctx, reason, "stack");
        if (!JS_IsUndefined(stack))
        {
            const char *text = JS_ToCString(ctx, stack);
            if (text) { snprintf(stackText, sizeof(stackText), "%s", text); JS_FreeCString(ctx, text); }
        }
        JS_FreeValue(ctx, stack);
    }
    vbjs_fatal_error("unhandled promise rejection", text, stackText);
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
    vbjs_log(line);
    vbjs_devtools_log(line);
    return JS_UNDEFINED;
}

// -------------------------------------------------------------- dgram module ---

static void vbjs_dgram_remove(vbjs_dgram_socket_t *socket)
{
    for (auto it = s_dgramSockets.begin(); it != s_dgramSockets.end(); ++it)
        if (*it == socket) { s_dgramSockets.erase(it); break; }
}

static void js_dgram_socket_finalizer(JSRuntime *rt, JSValue value)
{
    auto *socket = (vbjs_dgram_socket_t *)JS_GetOpaque(value, s_dgramSocketClass);
    if (!socket) return;
    if (socket->fd >= 0) close(socket->fd);
    vbjs_dgram_remove(socket);
    JS_FreeValueRT(rt, socket->listeners);
    delete socket;
}

static JSValue js_dgram_emit(JSContext *ctx, vbjs_dgram_socket_t *socket, const char *event, int argc, JSValueConst *argv)
{
    JSValue list = JS_GetPropertyStr(ctx, socket->listeners, event);
    if (JS_IsUndefined(list)) { JS_FreeValue(ctx, list); return JS_UNDEFINED; }
    int64_t length = 0; JS_GetLength(ctx, list, &length);
    for (uint32_t i = 0; i < (uint64_t)length; ++i)
    {
        JSValue listener = JS_GetPropertyUint32(ctx, list, i);
        if (JS_IsFunction(ctx, listener))
        {
            JSValue result = JS_Call(ctx, listener, JS_UNDEFINED, argc, argv);
            if (JS_IsException(result)) vbjs_dump_error(ctx, "uncaught exception in dgram handler");
            JS_FreeValue(ctx, result);
        }
        JS_FreeValue(ctx, listener);
    }
    JS_FreeValue(ctx, list);
    return JS_UNDEFINED;
}

static JSValue js_dgram_on(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    auto *socket = (vbjs_dgram_socket_t *)JS_GetOpaque2(ctx, this_val, s_dgramSocketClass);
    if (!socket) return JS_EXCEPTION;
    if (argc < 2 || !JS_IsFunction(ctx, argv[1])) return JS_ThrowTypeError(ctx, "socket.on(event, listener)");
    const char *event = JS_ToCString(ctx, argv[0]); if (!event) return JS_EXCEPTION;
    JSValue list = JS_GetPropertyStr(ctx, socket->listeners, event);
    if (JS_IsUndefined(list)) { JS_FreeValue(ctx, list); list = JS_NewArray(ctx); JS_SetPropertyStr(ctx, socket->listeners, event, JS_DupValue(ctx, list)); }
    int64_t length = 0; JS_GetLength(ctx, list, &length);
    JS_SetPropertyUint32(ctx, list, (uint32_t)length, JS_DupValue(ctx, argv[1]));
    JS_FreeValue(ctx, list); JS_FreeCString(ctx, event);
    return JS_DupValue(ctx, this_val);
}

static int js_dgram_resolve(JSContext *ctx, JSValueConst portValue, JSValueConst hostValue, int family, sockaddr_storage *address, socklen_t *addressLength)
{
    int32_t port = 0; if (JS_ToInt32(ctx, &port, portValue) || port < 0 || port > 65535) { JS_ThrowRangeError(ctx, "port must be between 0 and 65535"); return -1; }
    const char *host = JS_IsUndefined(hostValue) ? nullptr : JS_ToCString(ctx, hostValue);
    if (!JS_IsUndefined(hostValue) && !host) return -1;
    char service[16]; snprintf(service, sizeof(service), "%d", port);
    addrinfo hints {}; hints.ai_family = family; hints.ai_socktype = SOCK_DGRAM;
    addrinfo *result = nullptr; int const rc = getaddrinfo(host, service, &hints, &result);
    if (host) JS_FreeCString(ctx, host);
    if (rc || !result) { JS_ThrowReferenceError(ctx, "dgram: cannot resolve address"); return -1; }
    memcpy(address, result->ai_addr, result->ai_addrlen); *addressLength = (socklen_t)result->ai_addrlen; freeaddrinfo(result);
    return 0;
}

static JSValue js_dgram_bind(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    auto *socket = (vbjs_dgram_socket_t *)JS_GetOpaque2(ctx, this_val, s_dgramSocketClass);
    if (!socket) return JS_EXCEPTION;
    if (argc < 1) return JS_ThrowTypeError(ctx, "socket.bind(port[, address][, callback])");
    int callback = argc > 1 && JS_IsFunction(ctx, argv[argc-1]) ? argc - 1 : -1;
    JSValueConst host = callback > 1 ? argv[1] : JS_UNDEFINED;
    sockaddr_storage address {}; socklen_t addressLength = 0;
    if (js_dgram_resolve(ctx, argv[0], host, socket->family, &address, &addressLength)) return JS_EXCEPTION;
    if (bind(socket->fd, (sockaddr *)&address, addressLength)) return JS_ThrowReferenceError(ctx, "dgram.bind: cannot bind socket");
    js_dgram_emit(ctx, socket, "listening", 0, nullptr);
    if (callback >= 0) { JSValue result = JS_Call(ctx, argv[callback], JS_UNDEFINED, 0, nullptr); if (JS_IsException(result)) vbjs_dump_error(ctx, "uncaught exception in dgram bind callback"); JS_FreeValue(ctx, result); }
    return JS_DupValue(ctx, this_val);
}

static int js_dgram_copy_bytes(JSContext *ctx, JSValueConst value, std::vector<uint8_t> *bytes)
{
    if (JS_IsString(value)) { size_t length = 0; const char *text = JS_ToCStringLen(ctx, &length, value); if (!text) return -1; bytes->assign((const uint8_t *)text, (const uint8_t *)text + length); JS_FreeCString(ctx, text); return 0; }
    size_t offset = 0, length = 0, elementSize = 0; JSValue buffer = JS_GetTypedArrayBuffer(ctx, value, &offset, &length, &elementSize);
    if (!JS_IsException(buffer)) { size_t bufferLength = 0; uint8_t *base = JS_GetArrayBuffer(ctx, &bufferLength, buffer); if (base && offset + length <= bufferLength) { bytes->assign(base + offset, base + offset + length); JS_FreeValue(ctx, buffer); return 0; } JS_FreeValue(ctx, buffer); }
    return JS_ThrowTypeError(ctx, "dgram message must be a string or Uint8Array"), -1;
}

static JSValue js_dgram_send(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    auto *socket = (vbjs_dgram_socket_t *)JS_GetOpaque2(ctx, this_val, s_dgramSocketClass);
    if (!socket) return JS_EXCEPTION;
    if (argc < 2) return JS_ThrowTypeError(ctx, "socket.send(message, port[, address][, callback])");
    int callback = JS_IsFunction(ctx, argv[argc-1]) ? argc - 1 : -1;
    int const addressIndex = callback == 3 ? 2 : (callback < 0 && argc > 2 ? 2 : -1);
    sockaddr_storage address {}; socklen_t addressLength = 0;
    if (js_dgram_resolve(ctx, argv[1], addressIndex >= 0 ? argv[addressIndex] : JS_UNDEFINED, socket->family, &address, &addressLength)) return JS_EXCEPTION;
    std::vector<uint8_t> data;
    if (js_dgram_copy_bytes(ctx, argv[0], &data)) return JS_EXCEPTION;
    ssize_t const sent = sendto(socket->fd, data.data(), data.size(), 0, (sockaddr *)&address, addressLength);
    if (callback >= 0) { JSValue args[1] = { sent < 0 ? JS_NewError(ctx) : JS_NULL }; JSValue result = JS_Call(ctx, argv[callback], JS_UNDEFINED, 1, args); JS_FreeValue(ctx, args[0]); if (JS_IsException(result)) vbjs_dump_error(ctx, "uncaught exception in dgram send callback"); JS_FreeValue(ctx, result); }
    if (sent < 0) return JS_ThrowReferenceError(ctx, "dgram.send: cannot send datagram");
    return JS_UNDEFINED;
}

static JSValue js_dgram_close(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    auto *socket = (vbjs_dgram_socket_t *)JS_GetOpaque2(ctx, this_val, s_dgramSocketClass);
    if (!socket) return JS_EXCEPTION;
    if (socket->fd >= 0) { close(socket->fd); socket->fd = -1; js_dgram_emit(ctx, socket, "close", 0, nullptr); }
    if (argc && JS_IsFunction(ctx, argv[0])) { JSValue result = JS_Call(ctx, argv[0], JS_UNDEFINED, 0, nullptr); if (JS_IsException(result)) vbjs_dump_error(ctx, "uncaught exception in dgram close callback"); JS_FreeValue(ctx, result); }
    return JS_UNDEFINED;
}

static JSValue js_dgram_address(JSContext *ctx, JSValueConst this_val, int, JSValueConst *)
{
    auto *socket = (vbjs_dgram_socket_t *)JS_GetOpaque2(ctx, this_val, s_dgramSocketClass);
    if (!socket) return JS_EXCEPTION;
    sockaddr_storage address {}; socklen_t length = sizeof(address);
    if (socket->fd < 0 || getsockname(socket->fd, (sockaddr *)&address, &length)) return JS_ThrowReferenceError(ctx, "dgram.address: socket is closed");
    char host[NI_MAXHOST] = "", service[NI_MAXSERV] = ""; getnameinfo((sockaddr *)&address, length, host, sizeof(host), service, sizeof(service), NI_NUMERICHOST | NI_NUMERICSERV);
    JSValue result = JS_NewObject(ctx); JS_SetPropertyStr(ctx, result, "address", JS_NewString(ctx, host)); JS_SetPropertyStr(ctx, result, "port", JS_NewInt32(ctx, atoi(service))); JS_SetPropertyStr(ctx, result, "family", JS_NewString(ctx, socket->family == AF_INET6 ? "IPv6" : "IPv4")); return result;
}

static JSValue js_dgram_createSocket(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    if (argc < 1) return JS_ThrowTypeError(ctx, "dgram.createSocket(type[, callback])");
    const char *type = JS_ToCString(ctx, argv[0]); if (!type) return JS_EXCEPTION;
    int const family = !strcmp(type, "udp6") ? AF_INET6 : !strcmp(type, "udp4") ? AF_INET : 0; JS_FreeCString(ctx, type);
    if (!family) return JS_ThrowTypeError(ctx, "dgram.createSocket type must be 'udp4' or 'udp6'");
    auto *socket = new vbjs_dgram_socket_t; socket->family = family;
    socket->fd = ::socket(socket->family, SOCK_DGRAM, 0);
    if (socket->fd < 0) { delete socket; return JS_ThrowReferenceError(ctx, "dgram.createSocket: cannot create socket"); }
    fcntl(socket->fd, F_SETFL, fcntl(socket->fd, F_GETFL) | O_NONBLOCK); socket->listeners = JS_NewObject(ctx);
    JSValue object = JS_NewObjectClass(ctx, s_dgramSocketClass); JS_SetOpaque(object, socket); s_dgramSockets.push_back(socket);
    if (argc > 1 && JS_IsFunction(ctx, argv[1]))
    {
        JSValue args[2] = { JS_NewString(ctx, "message"), JS_DupValue(ctx, argv[1]) };
        JSValue result = js_dgram_on(ctx, object, 2, args);
        JS_FreeValue(ctx, result); JS_FreeValue(ctx, args[0]); JS_FreeValue(ctx, args[1]);
    }
    return object;
}

static void vbjs_dgram_poll(JSContext *ctx)
{
    for (auto *socket : s_dgramSockets)
    {
        if (socket->fd < 0) continue;
        for (;;)
        {
            uint8_t data[65536]; sockaddr_storage from {}; socklen_t fromLength = sizeof(from); ssize_t const received = recvfrom(socket->fd, data, sizeof(data), 0, (sockaddr *)&from, &fromLength);
            if (received < 0) { if (errno != EAGAIN && errno != EWOULDBLOCK) { JSValue error = JS_NewError(ctx); js_dgram_emit(ctx, socket, "error", 1, &error); JS_FreeValue(ctx, error); } break; }
            char host[NI_MAXHOST] = "", service[NI_MAXSERV] = ""; getnameinfo((sockaddr *)&from, fromLength, host, sizeof(host), service, sizeof(service), NI_NUMERICHOST | NI_NUMERICSERV);
            JSValue args[2] = { JS_NewUint8ArrayCopy(ctx, data, (size_t)received), JS_NewObject(ctx) }; JS_SetPropertyStr(ctx, args[1], "address", JS_NewString(ctx, host)); JS_SetPropertyStr(ctx, args[1], "port", JS_NewInt32(ctx, atoi(service))); JS_SetPropertyStr(ctx, args[1], "family", JS_NewString(ctx, from.ss_family == AF_INET6 ? "IPv6" : "IPv4")); JS_SetPropertyStr(ctx, args[1], "size", JS_NewInt32(ctx, (int)received)); js_dgram_emit(ctx, socket, "message", 2, args); JS_FreeValue(ctx, args[0]); JS_FreeValue(ctx, args[1]);
        }
    }
}

// --------------------------------------------------------------- net module ---

static void vbjs_net_remove(vbjs_net_socket_t *socket)
{
    for (auto it = s_netSockets.begin(); it != s_netSockets.end(); ++it)
        if (*it == socket) { s_netSockets.erase(it); break; }
}
static void js_net_socket_finalizer(JSRuntime *rt, JSValue value)
{
    auto *socket = (vbjs_net_socket_t *)JS_GetOpaque(value, s_netSocketClass);
    if (!socket) return; if (socket->fd >= 0) close(socket->fd); vbjs_net_remove(socket); JS_FreeValueRT(rt, socket->listeners); delete socket;
}
static void js_net_emit(JSContext *ctx, vbjs_net_socket_t *socket, const char *event, int argc, JSValueConst *argv)
{
    JSValue list = JS_GetPropertyStr(ctx, socket->listeners, event); if (JS_IsUndefined(list)) { JS_FreeValue(ctx, list); return; }
    int64_t length = 0; JS_GetLength(ctx, list, &length);
    for (uint32_t i = 0; i < (uint64_t)length; ++i) { JSValue listener = JS_GetPropertyUint32(ctx, list, i); if (JS_IsFunction(ctx, listener)) { JSValue result = JS_Call(ctx, listener, JS_UNDEFINED, argc, argv); if (JS_IsException(result)) vbjs_dump_error(ctx, "uncaught exception in net handler"); JS_FreeValue(ctx, result); } JS_FreeValue(ctx, listener); }
    JS_FreeValue(ctx, list);
}
static JSValue js_net_on(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    auto *socket = (vbjs_net_socket_t *)JS_GetOpaque2(ctx, this_val, s_netSocketClass); if (!socket) return JS_EXCEPTION;
    if (argc < 2 || !JS_IsFunction(ctx, argv[1])) return JS_ThrowTypeError(ctx, "socket.on(event, listener)");
    const char *event = JS_ToCString(ctx, argv[0]); if (!event) return JS_EXCEPTION;
    JSValue list = JS_GetPropertyStr(ctx, socket->listeners, event); if (JS_IsUndefined(list)) { JS_FreeValue(ctx, list); list = JS_NewArray(ctx); JS_SetPropertyStr(ctx, socket->listeners, event, JS_DupValue(ctx, list)); }
    int64_t length = 0; JS_GetLength(ctx, list, &length); JS_SetPropertyUint32(ctx, list, (uint32_t)length, JS_DupValue(ctx, argv[1])); JS_FreeValue(ctx, list); JS_FreeCString(ctx, event); return JS_DupValue(ctx, this_val);
}
static void js_net_close(JSContext *ctx, vbjs_net_socket_t *socket, int hadError)
{
    if (socket->closed) return; socket->closed = 1; if (socket->fd >= 0) { close(socket->fd); socket->fd = -1; } JSValue args[1] = { JS_NewBool(ctx, hadError) }; js_net_emit(ctx, socket, "close", 1, args); JS_FreeValue(ctx, args[0]);
}
static JSValue js_net_write(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    auto *socket = (vbjs_net_socket_t *)JS_GetOpaque2(ctx, this_val, s_netSocketClass); if (!socket) return JS_EXCEPTION;
    if (!argc) return JS_ThrowTypeError(ctx, "socket.write(data[, callback])"); if (socket->closed) return JS_ThrowReferenceError(ctx, "net.Socket is closed");
    std::vector<uint8_t> data; if (js_dgram_copy_bytes(ctx, argv[0], &data)) return JS_EXCEPTION;
    ssize_t const written = send(socket->fd, data.data(), data.size(), 0); int const failed = written < 0;
    if (argc > 1 && JS_IsFunction(ctx, argv[argc-1])) { JSValue result = JS_Call(ctx, argv[argc-1], JS_UNDEFINED, 0, nullptr); if (JS_IsException(result)) vbjs_dump_error(ctx, "uncaught exception in net write callback"); JS_FreeValue(ctx, result); }
    if (failed) { JSValue error = JS_NewError(ctx); js_net_emit(ctx, socket, "error", 1, &error); JS_FreeValue(ctx, error); return JS_FALSE; } return JS_TRUE;
}
static JSValue js_net_end(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    auto *socket = (vbjs_net_socket_t *)JS_GetOpaque2(ctx, this_val, s_netSocketClass); if (!socket) return JS_EXCEPTION;
    if (argc && !JS_IsFunction(ctx, argv[0])) { JSValue result = js_net_write(ctx, this_val, 1, argv); if (JS_IsException(result)) return result; JS_FreeValue(ctx, result); }
    if (!socket->closed) shutdown(socket->fd, SHUT_WR); if (argc && JS_IsFunction(ctx, argv[argc-1])) { JSValue result = JS_Call(ctx, argv[argc-1], JS_UNDEFINED, 0, nullptr); if (JS_IsException(result)) vbjs_dump_error(ctx, "uncaught exception in net end callback"); JS_FreeValue(ctx, result); } return JS_DupValue(ctx, this_val);
}
static JSValue js_net_destroy(JSContext *ctx, JSValueConst this_val, int, JSValueConst *)
{
    auto *socket = (vbjs_net_socket_t *)JS_GetOpaque2(ctx, this_val, s_netSocketClass); if (!socket) return JS_EXCEPTION; js_net_close(ctx, socket, 0); return JS_DupValue(ctx, this_val);
}
static JSValue js_net_setNoDelay(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    auto *socket = (vbjs_net_socket_t *)JS_GetOpaque2(ctx, this_val, s_netSocketClass); if (!socket) return JS_EXCEPTION; int enabled = 1; if (argc) enabled = JS_ToBool(ctx, argv[0]); setsockopt(socket->fd, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled)); return JS_DupValue(ctx, this_val);
}
static JSValue js_net_createConnection(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    if (!argc) return JS_ThrowTypeError(ctx, "net.createConnection(port[, host][, callback])"); int callback = JS_IsFunction(ctx, argv[argc-1]) ? argc - 1 : -1;
    int32_t port = 0; if (JS_ToInt32(ctx, &port, argv[0]) || port < 1 || port > 65535) return JS_ThrowRangeError(ctx, "port must be between 1 and 65535");
    int freeHost = 0; const char *host = "127.0.0.1";
    if (argc > 1 && callback != 1) { host = JS_ToCString(ctx, argv[1]); freeHost = 1; if (!host) return JS_EXCEPTION; }
    char service[16]; snprintf(service, sizeof(service), "%d", port); addrinfo hints {}; hints.ai_socktype = SOCK_STREAM; hints.ai_family = AF_UNSPEC; addrinfo *result = nullptr; int const rc = getaddrinfo(host, service, &hints, &result); if (freeHost) JS_FreeCString(ctx, host);
    if (rc || !result) return JS_ThrowReferenceError(ctx, "net.createConnection: cannot resolve host");
    auto *socket = new vbjs_net_socket_t; socket->fd = ::socket(result->ai_family, SOCK_STREAM, 0); if (socket->fd < 0) { freeaddrinfo(result); delete socket; return JS_ThrowReferenceError(ctx, "net.createConnection: cannot create socket"); }
    fcntl(socket->fd, F_SETFL, fcntl(socket->fd, F_GETFL) | O_NONBLOCK); int const connectResult = connect(socket->fd, result->ai_addr, result->ai_addrlen); freeaddrinfo(result);
    if (connectResult && errno != EINPROGRESS) { close(socket->fd); delete socket; return JS_ThrowReferenceError(ctx, "net.createConnection: cannot connect"); }
    socket->connecting = connectResult != 0; socket->listeners = JS_NewObject(ctx); JSValue object = JS_NewObjectClass(ctx, s_netSocketClass); JS_SetOpaque(object, socket); s_netSockets.push_back(socket);
    if (callback >= 0) { JSValue args[2] = { JS_NewString(ctx, "connect"), JS_DupValue(ctx, argv[callback]) }; JSValue ignored = js_net_on(ctx, object, 2, args); JS_FreeValue(ctx, ignored); JS_FreeValue(ctx, args[0]); JS_FreeValue(ctx, args[1]); }
    return object;
}
static void vbjs_net_poll(JSContext *ctx)
{
    for (auto *socket : s_netSockets) { if (socket->closed) continue; if (socket->connecting) { fd_set writeSet; FD_ZERO(&writeSet); FD_SET(socket->fd, &writeSet); timeval timeout {}; int const ready = select(socket->fd + 1, nullptr, &writeSet, nullptr, &timeout); if (ready > 0) { int error = 0; socklen_t length = sizeof(error); getsockopt(socket->fd, SOL_SOCKET, SO_ERROR, &error, &length); if (!error) { socket->connecting = 0; js_net_emit(ctx, socket, "connect", 0, nullptr); } else { JSValue value = JS_NewError(ctx); js_net_emit(ctx, socket, "error", 1, &value); JS_FreeValue(ctx, value); js_net_close(ctx, socket, 1); } } } if (socket->connecting) continue; for (;;) { uint8_t data[65536]; ssize_t const received = recv(socket->fd, data, sizeof(data), 0); if (received > 0) { JSValue value = JS_NewUint8ArrayCopy(ctx, data, (size_t)received); js_net_emit(ctx, socket, "data", 1, &value); JS_FreeValue(ctx, value); continue; } if (!received) { js_net_emit(ctx, socket, "end", 0, nullptr); js_net_close(ctx, socket, 0); } else if (errno != EAGAIN && errno != EWOULDBLOCK) { JSValue value = JS_NewError(ctx); js_net_emit(ctx, socket, "error", 1, &value); JS_FreeValue(ctx, value); js_net_close(ctx, socket, 1); } break; } }
}

// ------------------------------------------------------------ fetch (libcurl) ---

static size_t vbjs_fetch_write(char *data, size_t size, size_t count, void *opaque)
{
    auto *result = (vbjs_fetch_result_t *)opaque; result->body.append(data, size * count); return size * count;
}
static size_t vbjs_fetch_header(char *data, size_t size, size_t count, void *opaque)
{
    auto *result = (vbjs_fetch_result_t *)opaque; std::string line(data, size * count); size_t const colon = line.find(':');
    if (colon != std::string::npos) { std::string name = line.substr(0, colon), value = line.substr(colon + 1); while (!value.empty() && isspace((unsigned char)value.front())) value.erase(value.begin()); while (!value.empty() && isspace((unsigned char)value.back())) value.pop_back(); for (char &c : name) c = (char)tolower((unsigned char)c); result->headers[name] = value; }
    return size * count;
}
static void *vbjs_fetch_thread(void *opaque)
{
    auto *request = (vbjs_fetch_request_t *)opaque; CURL *curl = curl_easy_init();
    if (!curl) request->result->error = "fetch: unable to create HTTP client";
    else
    {
        curl_slist *headers = nullptr; for (auto const &header : request->headers) headers = curl_slist_append(headers, header.c_str()); if (headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_URL, request->url); curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L); curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L); curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L); curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, vbjs_fetch_write); curl_easy_setopt(curl, CURLOPT_WRITEDATA, request->result); curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, vbjs_fetch_header); curl_easy_setopt(curl, CURLOPT_HEADERDATA, request->result);
        if (request->method && strcmp(request->method, "GET")) curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request->method);
        if (!request->body.empty()) { curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request->body.data()); curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)request->body.size()); }
        CURLcode const code = curl_easy_perform(curl); if (code != CURLE_OK) request->result->error = curl_easy_strerror(code); else { char *effectiveUrl = nullptr; curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &request->result->status); curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &effectiveUrl); if (effectiveUrl) request->result->url = effectiveUrl; } curl_slist_free_all(headers); curl_easy_cleanup(curl);
    }
    free(request->url); free(request->method);
    pthread_mutex_lock(&s_fetchMutex);
    if (!s_fetchShuttingDown) s_fetchReady.push_back(request->result);
    else delete request->result; // The owning QuickJS runtime is gone; do not touch JSValue handles.
    pthread_mutex_unlock(&s_fetchMutex); delete request; return nullptr;
}
static JSValue js_fetch_body(JSContext *ctx, JSValueConst this_val, int json)
{
    JSValue resolving[2]; JSValue promise = JS_NewPromiseCapability(ctx, resolving); JSValue body = JS_GetPropertyStr(ctx, this_val, "_body"); JSValue value = body;
    if (json) { const char *text = JS_ToCString(ctx, body); value = text ? JS_ParseJSON(ctx, text, strlen(text), "fetch response") : JS_EXCEPTION; if (text) JS_FreeCString(ctx, text); JS_FreeValue(ctx, body); }
    JSValue callback = JS_IsException(value) ? resolving[1] : resolving[0]; JSValue argument = JS_IsException(value) ? JS_GetException(ctx) : value; JSValue ignored = JS_Call(ctx, callback, JS_UNDEFINED, 1, &argument); JS_FreeValue(ctx, ignored); JS_FreeValue(ctx, argument); JS_FreeValue(ctx, resolving[0]); JS_FreeValue(ctx, resolving[1]); return promise;
}
static JSValue js_fetch_text(JSContext *ctx, JSValueConst this_val, int, JSValueConst *) { return js_fetch_body(ctx, this_val, 0); }
static JSValue js_fetch_json(JSContext *ctx, JSValueConst this_val, int, JSValueConst *) { return js_fetch_body(ctx, this_val, 1); }
static JSValue js_fetch_arrayBuffer(JSContext *ctx, JSValueConst this_val, int, JSValueConst *)
{
    JSValue resolving[2], promise = JS_NewPromiseCapability(ctx, resolving); if (JS_IsException(promise)) return promise;
    size_t offset = 0, length = 0, elementSize = 0;
    JSValue bytes = JS_GetPropertyStr(ctx, this_val, "_bytes"), buffer = JS_GetTypedArrayBuffer(ctx, bytes, &offset, &length, &elementSize); JS_FreeValue(ctx, bytes);
    JSValue callback = JS_IsException(buffer) ? resolving[1] : resolving[0]; JSValue argument = JS_IsException(buffer) ? JS_GetException(ctx) : buffer; JSValue ignored = JS_Call(ctx, callback, JS_UNDEFINED, 1, &argument); JS_FreeValue(ctx, ignored); JS_FreeValue(ctx, argument); JS_FreeValue(ctx, resolving[0]); JS_FreeValue(ctx, resolving[1]); return promise;
}
static JSValue js_fetch_headers_get(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    if (!argc) return JS_NULL; const char *name = JS_ToCString(ctx, argv[0]); if (!name) return JS_EXCEPTION; std::string key(name); JS_FreeCString(ctx, name); for (char &c : key) c = (char)tolower((unsigned char)c); JSValue values = JS_GetPropertyStr(ctx, this_val, "_values"); JSValue value = JS_GetPropertyStr(ctx, values, key.c_str()); JS_FreeValue(ctx, values); if (JS_IsUndefined(value)) { JS_FreeValue(ctx, value); return JS_NULL; } return value;
}
static JSValue js_fetch(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    if (!argc) return JS_ThrowTypeError(ctx, "fetch(url[, options])"); const char *url = JS_ToCString(ctx, argv[0]); if (!url) return JS_EXCEPTION;
    auto *result = new vbjs_fetch_result_t; JSValue resolving[2]; JSValue promise = JS_NewPromiseCapability(ctx, resolving); result->resolve = resolving[0]; result->reject = resolving[1]; auto *request = new vbjs_fetch_request_t {}; request->url = strdup(url); JS_FreeCString(ctx, url); request->result = result;
    if (argc > 1 && JS_IsObject(argv[1])) { JSValue method = JS_GetPropertyStr(ctx, argv[1], "method"), body = JS_GetPropertyStr(ctx, argv[1], "body"), headers = JS_GetPropertyStr(ctx, argv[1], "headers"); if (JS_IsString(method)) { const char *text = JS_ToCString(ctx, method); request->method = text ? strdup(text) : nullptr; if (text) JS_FreeCString(ctx, text); } if (!JS_IsUndefined(body) && !JS_IsNull(body) && js_dgram_copy_bytes(ctx, body, &request->body)) { JS_FreeValue(ctx, method); JS_FreeValue(ctx, body); JS_FreeValue(ctx, headers); free(request->url); free(request->method); delete request; JS_FreeValue(ctx, result->resolve); JS_FreeValue(ctx, result->reject); delete result; return JS_EXCEPTION; } if (JS_IsObject(headers)) { JSPropertyEnum *names = nullptr; uint32_t count = 0; if (!JS_GetOwnPropertyNames(ctx, &names, &count, headers, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY)) { for (uint32_t i = 0; i < count; ++i) { char const *name = JS_AtomToCString(ctx, names[i].atom); JSValue value = JS_GetProperty(ctx, headers, names[i].atom); char const *text = JS_ToCString(ctx, value); if (name && text) request->headers.emplace_back(std::string(name) + ": " + text); if (name) JS_FreeCString(ctx, name); if (text) JS_FreeCString(ctx, text); JS_FreeValue(ctx, value); JS_FreeAtom(ctx, names[i].atom); } js_free(ctx, names); } } JS_FreeValue(ctx, method); JS_FreeValue(ctx, body); JS_FreeValue(ctx, headers); }
    pthread_t thread; if (pthread_create(&thread, nullptr, vbjs_fetch_thread, request)) { free(request->url); free(request->method); delete request; JS_FreeValue(ctx, result->resolve); JS_FreeValue(ctx, result->reject); delete result; return JS_ThrowInternalError(ctx, "fetch: cannot create worker thread"); } pthread_detach(thread); return promise;
}
static void vbjs_fetch_drain(JSContext *ctx)
{
    std::deque<vbjs_fetch_result_t *> ready; pthread_mutex_lock(&s_fetchMutex); ready.swap(s_fetchReady); pthread_mutex_unlock(&s_fetchMutex);
    for (auto *result : ready) { JSValue argument; JSValue callback; if (!result->error.empty()) { argument = JS_NewError(ctx); JS_SetPropertyStr(ctx, argument, "message", JS_NewString(ctx, result->error.c_str())); callback = result->reject; } else { argument = JS_NewObject(ctx); JS_SetPropertyStr(ctx, argument, "status", JS_NewInt32(ctx, (int)result->status)); JS_SetPropertyStr(ctx, argument, "ok", JS_NewBool(ctx, result->status >= 200 && result->status < 300)); JS_SetPropertyStr(ctx, argument, "url", JS_NewString(ctx, result->url.c_str())); JS_SetPropertyStr(ctx, argument, "_body", JS_NewStringLen(ctx, result->body.data(), result->body.size())); JS_SetPropertyStr(ctx, argument, "_bytes", JS_NewUint8ArrayCopy(ctx, (const uint8_t *)result->body.data(), result->body.size())); JS_SetPropertyStr(ctx, argument, "text", JS_NewCFunction(ctx, js_fetch_text, "text", 0)); JS_SetPropertyStr(ctx, argument, "json", JS_NewCFunction(ctx, js_fetch_json, "json", 0)); JS_SetPropertyStr(ctx, argument, "arrayBuffer", JS_NewCFunction(ctx, js_fetch_arrayBuffer, "arrayBuffer", 0)); JSValue headers = JS_NewObject(ctx), values = JS_NewObject(ctx); for (auto const &header : result->headers) JS_SetPropertyStr(ctx, values, header.first.c_str(), JS_NewString(ctx, header.second.c_str())); JS_SetPropertyStr(ctx, headers, "_values", values); JS_SetPropertyStr(ctx, headers, "get", JS_NewCFunction(ctx, js_fetch_headers_get, "get", 1)); JS_SetPropertyStr(ctx, argument, "headers", headers); callback = result->resolve; } JSValue ignored = JS_Call(ctx, callback, JS_UNDEFINED, 1, &argument); JS_FreeValue(ctx, ignored); JS_FreeValue(ctx, argument); JS_FreeValue(ctx, result->resolve); JS_FreeValue(ctx, result->reject); delete result; }
}

// ----------------------------------------------------------------- fs module ---

static int js_fs_text_encoding(JSContext *ctx, int argc, JSValueConst *argv, int optionIndex)
{
    if (argc <= optionIndex || JS_IsUndefined(argv[optionIndex])) return 0;
    if (JS_IsNull(argv[optionIndex])) return 0;
    if (JS_IsString(argv[optionIndex])) return 1;
    if (JS_IsObject(argv[optionIndex]))
    {
        JSValue encoding = JS_GetPropertyStr(ctx, argv[optionIndex], "encoding");
        int const text = JS_IsString(encoding);
        JS_FreeValue(ctx, encoding);
        return text;
    }
    return 0;
}

static JSValue js_fs_readFile_impl(JSContext *ctx, int argc, JSValueConst *argv, int legacyText)
{
    if (argc < 1) return JS_ThrowTypeError(ctx, "fs.readFile(path)");
    const char *path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;
    size_t len = 0; char *buf = vbjs_read_file(path, &len);
    JS_FreeCString(ctx, path);
    if (!buf) return JS_ThrowReferenceError(ctx, "fs.readFile: cannot open file");
    JSValue v = (legacyText || js_fs_text_encoding(ctx, argc, argv, 1))
        ? JS_NewStringLen(ctx, buf, len) : JS_NewUint8ArrayCopy(ctx, (const uint8_t *)buf, len);
    free(buf);
    return v;
}
static JSValue js_fs_readFile(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{ return js_fs_readFile_impl(ctx, argc, argv, 1); } // legacy VibeBuild32 form
static JSValue js_fs_readFileSync_node(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{ return js_fs_readFile_impl(ctx, argc, argv, 0); }

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

using vbjs_fs_function_t = JSValue (*)(JSContext *, JSValueConst, int, JSValueConst *);

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

static JSValue js_fs_unlinkSync(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    if (argc < 1) return JS_ThrowTypeError(ctx, "fs.unlinkSync(path)");
    const char *path = JS_ToCString(ctx, argv[0]); if (!path) return JS_EXCEPTION;
    int const ok = unlink(path) == 0; JS_FreeCString(ctx, path);
    return ok ? JS_UNDEFINED : JS_ThrowReferenceError(ctx, "fs.unlink: cannot remove file");
}
static JSValue js_fs_renameSync(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    if (argc < 2) return JS_ThrowTypeError(ctx, "fs.renameSync(oldPath, newPath)");
    const char *from = JS_ToCString(ctx, argv[0]), *to = JS_ToCString(ctx, argv[1]);
    if (!from || !to) { if (from) JS_FreeCString(ctx, from); if (to) JS_FreeCString(ctx, to); return JS_EXCEPTION; }
    int const ok = rename(from, to) == 0; JS_FreeCString(ctx, from); JS_FreeCString(ctx, to);
    return ok ? JS_UNDEFINED : JS_ThrowReferenceError(ctx, "fs.rename: cannot rename file");
}
static JSValue js_fs_mkdirSync(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    if (!argc) return JS_ThrowTypeError(ctx, "fs.mkdirSync(path[, options])"); const char *path = JS_ToCString(ctx, argv[0]); if (!path) return JS_EXCEPTION; int const ok = mkdir(path, 0755) == 0 || errno == EEXIST; JS_FreeCString(ctx, path); return ok ? JS_UNDEFINED : JS_ThrowReferenceError(ctx, "fs.mkdirSync: cannot create directory");
}
static JSValue js_fs_stat_isFile(JSContext *ctx, JSValueConst this_val, int, JSValueConst *) { JSValue mode = JS_GetPropertyStr(ctx, this_val, "_mode"); int32_t value = 0; JS_ToInt32(ctx, &value, mode); JS_FreeValue(ctx, mode); return JS_NewBool(ctx, S_ISREG(value)); }
static JSValue js_fs_stat_isDirectory(JSContext *ctx, JSValueConst this_val, int, JSValueConst *) { JSValue mode = JS_GetPropertyStr(ctx, this_val, "_mode"); int32_t value = 0; JS_ToInt32(ctx, &value, mode); JS_FreeValue(ctx, mode); return JS_NewBool(ctx, S_ISDIR(value)); }
static JSValue js_fs_readdirSync(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    if (!argc) return JS_ThrowTypeError(ctx, "fs.readdirSync(path[, options])"); const char *path = JS_ToCString(ctx, argv[0]); if (!path) return JS_EXCEPTION; DIR *dir = opendir(path); JS_FreeCString(ctx, path); if (!dir) return JS_ThrowReferenceError(ctx, "fs.readdirSync: cannot open directory"); JSValue entries = JS_NewArray(ctx); uint32_t index = 0; while (dirent *entry = readdir(dir)) if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..")) JS_SetPropertyUint32(ctx, entries, index++, JS_NewString(ctx, entry->d_name)); closedir(dir); return entries;
}
static JSValue js_fs_statSync(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    if (!argc) return JS_ThrowTypeError(ctx, "fs.statSync(path[, options])"); const char *path = JS_ToCString(ctx, argv[0]); if (!path) return JS_EXCEPTION; struct stat info {}; int const ok = stat(path, &info) == 0; JS_FreeCString(ctx, path); if (!ok) return JS_ThrowReferenceError(ctx, "fs.statSync: cannot stat path"); JSValue result = JS_NewObject(ctx); JS_SetPropertyStr(ctx, result, "size", JS_NewFloat64(ctx, (double)info.st_size)); JS_SetPropertyStr(ctx, result, "isFile", JS_NewCFunction(ctx, js_fs_stat_isFile, "isFile", 0)); JS_SetPropertyStr(ctx, result, "isDirectory", JS_NewCFunction(ctx, js_fs_stat_isDirectory, "isDirectory", 0)); JS_SetPropertyStr(ctx, result, "_mode", JS_NewInt32(ctx, (int)info.st_mode)); return result;
}

// Callback APIs are queued to the next engine tick. The file operation itself
// is currently synchronous, but callbacks never run re-entrantly from the
// JavaScript call that initiated them.
static JSValue js_fs_callback_call(JSContext *ctx, int argc, JSValueConst *argv, vbjs_fs_function_t function, int existsStyle = 0)
{
    if (argc < 2 || !JS_IsFunction(ctx, argv[argc - 1]))
        return JS_EXCEPTION;
    JSValue result = function(ctx, JS_UNDEFINED, argc - 1, argv);
    int const error = JS_IsException(result);
    JSValue value = error ? JS_GetException(ctx) : result;
    s_fsCallbacks.push_back({ JS_DupValue(ctx, argv[argc - 1]), value, error, existsStyle });
    return JS_UNDEFINED;
}

static JSValue js_fs_readFile_node(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    if (argc > 1 && JS_IsFunction(ctx, argv[argc - 1])) return js_fs_callback_call(ctx, argc, argv, js_fs_readFileSync_node);
    return js_fs_readFileSync_node(ctx, JS_UNDEFINED, argc, argv);
}
static JSValue js_fs_writeFile_node(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    if (argc > 2 && JS_IsFunction(ctx, argv[argc - 1])) return js_fs_callback_call(ctx, argc, argv, js_fs_writeFile);
    return js_fs_writeFile(ctx, JS_UNDEFINED, argc, argv);
}
static JSValue js_fs_appendFile_node(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    if (argc > 2 && JS_IsFunction(ctx, argv[argc - 1])) return js_fs_callback_call(ctx, argc, argv, js_fs_appendFile);
    return js_fs_appendFile(ctx, JS_UNDEFINED, argc, argv);
}
static JSValue js_fs_exists_node(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    if (argc > 1 && JS_IsFunction(ctx, argv[argc - 1])) return js_fs_callback_call(ctx, argc, argv, js_fs_exists, 1);
    return js_fs_exists(ctx, JS_UNDEFINED, argc, argv);
}
static JSValue js_fs_unlink_node(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{ return argc > 1 && JS_IsFunction(ctx, argv[argc - 1]) ? js_fs_callback_call(ctx, argc, argv, js_fs_unlinkSync) : js_fs_unlinkSync(ctx, JS_UNDEFINED, argc, argv); }
static JSValue js_fs_rename_node(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{ return argc > 2 && JS_IsFunction(ctx, argv[argc - 1]) ? js_fs_callback_call(ctx, argc, argv, js_fs_renameSync) : js_fs_renameSync(ctx, JS_UNDEFINED, argc, argv); }

// The engine filesystem is synchronous, but expose it through genuine
// JavaScript Promises so `await fs.promises.*()` has normal Promise/error
// semantics and composes with the rest of a mod's async code.
static JSValue js_fs_promise_call(JSContext *ctx, int argc, JSValueConst *argv, vbjs_fs_function_t function)
{
    JSValue resolving[2];
    JSValue promise = JS_NewPromiseCapability(ctx, resolving);
    if (JS_IsException(promise)) return promise;

    JSValue result = function(ctx, JS_UNDEFINED, argc, argv);
    int const rejected = JS_IsException(result);
    JSValue argument = rejected ? JS_GetException(ctx) : result;
    JSValue settled = JS_Call(ctx, resolving[rejected ? 1 : 0], JS_UNDEFINED, 1, &argument);
    JS_FreeValue(ctx, settled);
    JS_FreeValue(ctx, argument);
    JS_FreeValue(ctx, resolving[0]);
    JS_FreeValue(ctx, resolving[1]);
    return promise;
}

static JSValue js_fs_promises_readFile(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{ return js_fs_promise_call(ctx, argc, argv, js_fs_readFileSync_node); }
static JSValue js_fs_promises_writeFile(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{ return js_fs_promise_call(ctx, argc, argv, js_fs_writeFile); }
static JSValue js_fs_promises_appendFile(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{ return js_fs_promise_call(ctx, argc, argv, js_fs_appendFile); }
static JSValue js_fs_promises_exists(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{ return js_fs_promise_call(ctx, argc, argv, js_fs_exists); }
static JSValue js_fs_promises_unlink(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{ return js_fs_promise_call(ctx, argc, argv, js_fs_unlinkSync); }
static JSValue js_fs_promises_rename(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{ return js_fs_promise_call(ctx, argc, argv, js_fs_renameSync); }

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
    if (!strcmp(eventKey, "frame")) s_frameEventListened = 1;
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
        if (!strcmp(eventKey, "frame")) s_frameEventListened = 0;
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
            if (!JS_IsFunction(ctx, fn))
            {
                JS_FreeValue(ctx, fn);
                continue;
            }
            JSValue r  = JS_Call(ctx, fn, JS_UNDEFINED, argc, (JSValueConst *)argv);
            if (JS_IsException(r))
            {
                char context[128];
                snprintf(context, sizeof(context), "uncaught exception in %s handler #%u (disabled)", event, i + 1);
                vbjs_dump_error(ctx, context);
                // An uncaught handler must not destabilize the game or throw on
                // every subsequent frame/input event.  It remains visible in
                // the logs and can be registered again after the mod is fixed.
                JS_SetPropertyUint32(ctx, arr, i, JS_UNDEFINED);
            }
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

static void vbjs_set_string(JSContext *ctx, JSValueConst object, const char *name, const char *value)
{
    JS_SetPropertyStr(ctx, object, name, value ? JS_NewString(ctx, value) : JS_UNDEFINED);
}

// A snapshot of real engine state.  Values are intentionally copied, so a
// script can keep it without holding a pointer into mutable game structures.
static JSValue js_vb_session(JSContext *ctx, JSValueConst, int, JSValueConst *)
{
    JSValue session = JS_NewObject(ctx);
    int const host = vbjs_game::hostPlayer();
    JS_SetPropertyStr(ctx, session, "started", JS_NewBool(ctx, vbjs_game::gameStarted()));
    JS_SetPropertyStr(ctx, session, "skill", JS_NewInt32(ctx, vbjs_game::skill()));
    JS_SetPropertyStr(ctx, session, "episode", JS_NewInt32(ctx, vbjs_game::episode()));
    JS_SetPropertyStr(ctx, session, "level", JS_NewInt32(ctx, vbjs_game::level()));
    JS_SetPropertyStr(ctx, session, "localPlayer", JS_NewInt32(ctx, vbjs_game::localPlayer()));
    JS_SetPropertyStr(ctx, session, "hostPlayer", host >= 0 ? JS_NewInt32(ctx, host) : JS_UNDEFINED);
    vbjs_set_string(ctx, session, "hostName", host >= 0 ? vbjs_game::playerName(host) : nullptr);
    vbjs_set_string(ctx, session, "episodeName", vbjs_game::episodeName());
    vbjs_set_string(ctx, session, "levelName", vbjs_game::levelName());
    vbjs_set_string(ctx, session, "userMap", vbjs_game::userMap());
    vbjs_set_string(ctx, session, "serverAddress", vbjs_game::connectedServer());
    JSValue players = JS_NewArray(ctx);
    int const count = vbjs_game::playerCount();
    for (int ordinal = 0; ordinal < count; ++ordinal)
    {
        int const index = vbjs_game::playerAt(ordinal);
        if (index < 0) continue;
        JSValue player = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, player, "index", JS_NewInt32(ctx, index));
        JS_SetPropertyStr(ctx, player, "sprite", JS_NewInt32(ctx, vbjs_game::playerSpriteAt(index)));
        JS_SetPropertyStr(ctx, player, "isLocal", JS_NewBool(ctx, index == vbjs_game::localPlayer()));
        JS_SetPropertyStr(ctx, player, "isHost", JS_NewBool(ctx, index == host));
        const char *name = vbjs_game::playerName(index);
        JS_SetPropertyStr(ctx, player, "name", JS_NewString(ctx, name ? name : ""));
        JS_SetPropertyUint32(ctx, players, ordinal, player);
    }
    JS_SetPropertyStr(ctx, session, "players", players);
    return session;
}

static JSValue js_vb_setMenuText(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    if (argc < 2) return JS_ThrowTypeError(ctx, "vb.setMenuText(original, replacement)");
    const char *original = JS_ToCString(ctx, argv[0]);
    const char *replacement = JS_ToCString(ctx, argv[1]);
    if (!original || !replacement) { if (original) JS_FreeCString(ctx, original); if (replacement) JS_FreeCString(ctx, replacement); return JS_EXCEPTION; }
    s_menuTextOverrides[original] = replacement;
    JS_FreeCString(ctx, original); JS_FreeCString(ctx, replacement);
    return JS_UNDEFINED;
}

static JSValue js_vb_setMenuBackground(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    int32_t tile = -1;
    if (argc > 0 && !JS_IsNull(argv[0]) && !JS_IsUndefined(argv[0]) && JS_ToInt32(ctx, &tile, argv[0]))
        return JS_EXCEPTION;
    vbjs_game::setMenuBackground(tile);
    return JS_UNDEFINED;
}

static JSValue js_vb_currentMenu(JSContext *ctx, JSValueConst, int, JSValueConst *)
{
    return JS_NewInt32(ctx, vbjs_game::currentMenu());
}

static JSValue js_vb_openMenu(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    int32_t menu = 0;
    if (argc < 1 || JS_ToInt32(ctx, &menu, argv[0]))
        return JS_ThrowTypeError(ctx, "vb.openMenu(menuId)");
    vbjs_game::openMenu(menu);
    return JS_UNDEFINED;
}

static JSValue js_vb_setMenuRoute(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    int32_t menu = 0, item = 0, destination = 0;
    if (argc < 3 || JS_ToInt32(ctx, &menu, argv[0]) || JS_ToInt32(ctx, &item, argv[1]))
        return JS_ThrowTypeError(ctx, "vb.setMenuRoute(menuId, itemIndex, destination | null)");
    uint64_t const key = vbjs_menu_route_key(menu, item);
    if (JS_IsNull(argv[2]) || JS_IsUndefined(argv[2]))
        s_menuRoutes.erase(key);
    else
    {
        if (JS_ToInt32(ctx, &destination, argv[2])) return JS_EXCEPTION;
        s_menuRoutes[key] = destination;
    }
    return JS_UNDEFINED;
}

static JSValue js_vb_menuSelection(JSContext *ctx, JSValueConst, int, JSValueConst *)
{
    return JS_NewInt32(ctx, vbjs_game::menuSelection());
}

static JSValue js_vb_setMenuSelection(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    int32_t selection = 0;
    if (argc < 1 || JS_ToInt32(ctx, &selection, argv[0]))
        return JS_ThrowTypeError(ctx, "vb.setMenuSelection(index)");
    vbjs_game::setMenuSelection(selection);
    return JS_UNDEFINED;
}

// --------------------------------------------------- vb/fs export helpers ---

static void vbjs_setup_components(JSContext *ctx)
{
    // Keep this in JavaScript: user subclasses then have normal JS/TS class
    // semantics, while the lifecycle itself is driven by real engine events.
    static const char *source =
        "(function(vb){\n"
        "  const entities = Object.create(null); let nextNodeId = 1;\n"
        "  const call = (component, name, ...args) => { const fn = component[name]; if (typeof fn === 'function') fn.apply(component, args); };\n"
        "  class VBNode { constructor(){ this.id = nextNodeId++; this.name = 'Node ' + this.id; this.tag = 'Untagged'; this.destroyed = false; this._activeSelf = true; Object.defineProperty(this, 'activeSelf', { enumerable: true, get: () => this._activeSelf, set: active => setNodeActive(this, active) }); this.transform = { position: { x: 0, y: 0, z: 0 }, rotation: { x: 0, y: 0, z: 0 }, scale: { x: 1, y: 1, z: 1 } }; }\n"
        "    get activeInHierarchy(){ return !this.destroyed && this._activeSelf; } SetActive(active){ return setNodeActive(this, active); } AddComponent(type){ return attach(this, type); } GetComponent(type){ return getComponent(this, type); } GetComponents(type){ return getComponents(this, type); } Destroy(){ return destroyNode(this); } }\n"
        "  const key = entity => entity instanceof VBNode ? 'node:' + entity.id : 'sprite:' + entity;\n"
        "  const list = entity => entities[key(entity)] || (entities[key(entity)] = []);\n"
        "  const active = component => component.enabled && (!component.node || component.node.activeInHierarchy);\n"
        "  const entityOf = component => component.sprite >= 0 ? component.sprite : component.node;\n"
        "  const release = component => { if (active(component)) call(component, 'onDisable'); call(component, 'onDestroy'); trackUpdates(component, -1); component.sprite = -1; component.node = null; };\n"
        "  class VBComponent { constructor(){ this.sprite = -1; this.node = null; this._enabled = true; Object.defineProperty(this, 'enabled', { enumerable: true, get: () => this._enabled, set: value => setComponentEnabled(this, value) }); Object.defineProperty(this, 'transform', { enumerable: true, get: () => this.node ? this.node.transform : undefined }); }\n"
        "    get gameObject(){ return this.node; } get isActiveAndEnabled(){ return active(this); } GetComponent(type){ const entity = entityOf(this); return entity === null ? undefined : getComponent(entity, type); } GetComponents(type){ const entity = entityOf(this); return entity === null ? [] : getComponents(entity, type); } AddComponent(type){ const entity = entityOf(this); return entity === null ? undefined : attach(entity, type); } Destroy(){ const entity = entityOf(this); return entity === null ? false : detach(entity, this); }\n"
        "    awake(){} start(){} onSpawn(){} fixedUpdate(dt){} update(dt){} lateUpdate(dt){} onDestroy(){} onEnable(){} onDisable(){} }\n"
        "  class VBPrefab { constructor(tile, ...components){ this.tile = tile; this.components = components; } }\n"
        "  const updateNames = ['fixedUpdate', 'update', 'lateUpdate']; const updateCounts = { fixedUpdate: 0, update: 0, lateUpdate: 0 };\n"
        "  const trackUpdates = (component, delta) => { for (const name of updateNames) if (component[name] !== VBComponent.prototype[name]) updateCounts[name] += delta; };\n"
        "  function attach(entity, componentOrType) {\n"
        "    if (!(entity instanceof VBNode) && (!Number.isInteger(entity) || entity < 0)) throw new TypeError('vb.attach(entity, component) requires a sprite or VBNode');\n"
        "    if (entity instanceof VBNode && entity.destroyed) throw new Error('Cannot attach to a destroyed VBNode');\n"
        "    const component = typeof componentOrType === 'function' ? new componentOrType() : componentOrType;\n"
        "    if (!(component instanceof VBComponent)) throw new TypeError('Components must extend VBComponent');\n"
        "    if (component.sprite >= 0 || component.node) throw new Error('Component is already attached');\n"
        "    ensureComponentLifecycle(); component.sprite = entity instanceof VBNode ? -1 : entity; component.node = entity instanceof VBNode ? entity : null; trackUpdates(component, 1); if (updateNames.some(name => updateCounts[name])) ensureComponentLoop(); list(entity).push(component); call(component, 'awake'); if (active(component)) call(component, 'onEnable'); call(component, 'onSpawn'); call(component, 'start'); return component;\n"
        "  }\n"
        "  function detach(entity, component) { const entityKey = key(entity), components = entities[entityKey]; if (!components) return false; const index = components.indexOf(component); if (index < 0) return false; components.splice(index, 1); release(component); if (!components.length) delete entities[entityKey]; return true; }\n"
        "  function destroyComponents(entity) { const entityKey = key(entity), components = entities[entityKey]; if (!components) return; delete entities[entityKey]; for (const component of components.slice()) release(component); }\n"
        "  function getComponent(entity, type) { const components = entities[key(entity)] || []; return components.find(component => component instanceof type); }\n"
        "  function getComponents(entity, type) { const components = (entities[key(entity)] || []).slice(); return type ? components.filter(component => component instanceof type) : components; }\n"
        "  function setComponentEnabled(component, enabled) { enabled = !!enabled; if (component._enabled === enabled) return; const wasActive = active(component); component._enabled = enabled; if (wasActive && !active(component)) call(component, 'onDisable'); else if (!wasActive && active(component)) call(component, 'onEnable'); }\n"
        "  function createPrefab(tile, ...components) { return new VBPrefab(tile, ...components); }\n"
        "  function spawnPrefab(prefab, x, y, z, owner) { if (!(prefab instanceof VBPrefab)) throw new TypeError('spawnPrefab expects a VBPrefab'); const sprite = vb.spawn(prefab.tile, x, y, z, owner); if (sprite >= 0) for (const type of prefab.components) attach(sprite, type); return sprite; }\n"
        "  function createNode(...components) { const node = new VBNode(); for (const type of components) attach(node, type); return node; }\n"
        "  function setNodeActive(node, enabled) { if (!(node instanceof VBNode) || node.destroyed) return false; enabled = !!enabled; if (node._activeSelf === enabled) return true; const components = (entities[key(node)] || []).slice(); const wasActive = node.activeInHierarchy; node._activeSelf = enabled; for (const component of components) if (component.enabled) call(component, wasActive && !enabled ? 'onDisable' : !wasActive && enabled ? 'onEnable' : 'awake'); return true; }\n"
        "  function destroyNode(node) { if (!(node instanceof VBNode) || node.destroyed) return false; destroyComponents(node); node.destroyed = true; return true; }\n"
        "  const eachUpdate = (name, dt) => { const base = VBComponent.prototype[name]; for (const key of Object.keys(entities)) for (const component of entities[key].slice()) { const fn = component[name]; if (active(component) && fn !== base) fn.call(component, dt); } };\n"
        "  let componentLifecycleInstalled = false, componentLoopInstalled = false;\n"
        "  function ensureComponentLifecycle() { if (componentLifecycleInstalled) return; componentLifecycleInstalled = true; vb.on('actorDestroyed', event => destroyComponents(event.sprite)); }\n"
        "  function ensureComponentLoop() { if (componentLoopInstalled) return; componentLoopInstalled = true; let lastTime = 0, fixedAccum = 0;\n"
        "    vb.on('frame', () => { const now = Date.now(), dt = lastTime ? Math.min((now - lastTime) / 1000, .25) : 0; lastTime = now;\n"
        "      if (updateCounts.fixedUpdate) { fixedAccum = Math.min(fixedAccum + dt, .25); for (let steps = 0; fixedAccum >= 1 / 60 && steps < 4; ++steps) { eachUpdate('fixedUpdate', 1 / 60); fixedAccum -= 1 / 60; } }\n"
        "      if (updateCounts.update) eachUpdate('update', dt); if (updateCounts.lateUpdate) eachUpdate('lateUpdate', dt);\n"
        "    });\n"
        "  }\n"
        "  return { VBComponent, VBNode, VBPrefab, attach, detach, getComponent, getComponents, setComponentEnabled, createNode, destroyNode, createPrefab, spawnPrefab };\n"
        "})(vb)";
    JSValue value = JS_Eval(ctx, source, strlen(source), "<vb-components>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(value)) { vbjs_dump_error(ctx); return; }
    s_componentExports = value;
}

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
    JS_SetPropertyStr(ctx, target, "session",     JS_NewCFunction(ctx, js_vb_session,     "session", 0));
    JS_SetPropertyStr(ctx, target, "setMenuText", JS_NewCFunction(ctx, js_vb_setMenuText, "setMenuText", 2));
    JS_SetPropertyStr(ctx, target, "setMenuBackground", JS_NewCFunction(ctx, js_vb_setMenuBackground, "setMenuBackground", 1));
    JS_SetPropertyStr(ctx, target, "currentMenu", JS_NewCFunction(ctx, js_vb_currentMenu, "currentMenu", 0));
    JS_SetPropertyStr(ctx, target, "openMenu", JS_NewCFunction(ctx, js_vb_openMenu, "openMenu", 1));
    JS_SetPropertyStr(ctx, target, "setMenuRoute", JS_NewCFunction(ctx, js_vb_setMenuRoute, "setMenuRoute", 3));
    JS_SetPropertyStr(ctx, target, "menuSelection", JS_NewCFunction(ctx, js_vb_menuSelection, "menuSelection", 0));
    JS_SetPropertyStr(ctx, target, "setMenuSelection", JS_NewCFunction(ctx, js_vb_setMenuSelection, "setMenuSelection", 1));
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
    if (JS_IsObject(s_componentExports))
    {
        static const char *names[] = { "VBComponent", "VBNode", "VBPrefab", "attach", "detach", "getComponent", "getComponents", "setComponentEnabled", "createNode", "destroyNode", "createPrefab", "spawnPrefab" };
        for (auto name : names) JS_SetPropertyStr(ctx, target, name, JS_GetPropertyStr(ctx, s_componentExports, name));
    }

    // SW currently shares the JS host/debugger, but not Duke's game-state
    // bindings.  Convert accidental Duke calls into a normal JS exception,
    // which produces the in-game, copyable error dialog instead of a crash.
    if (s_genericMode)
    {
        static const char *unsupported[] = {
            "drawText", "clearText", "spawn", "A_Spawn", "A_InsertSprite", "sendMessage",
            "getGamevar", "setGamevar", "player", "session", "setMenuText", "setMenuBackground",
            "currentMenu", "openMenu", "setMenuRoute", "menuSelection", "setMenuSelection", "run",
            "define", "sound", "shoot", "addAmmo", "addWeapon", "getActor", "setActor", "getPlayer",
            "setPlayer", "getInput", "setInput", "getSector", "setSector", "getWall", "setWall",
            "getProjectile", "setProjectile", "getActiveProjectile", "setActiveProjectile", "getTileData",
            "setTileData", "getUserdef", "setUserdef", "VBComponent", "VBNode", "VBPrefab", "attach",
            "detach", "getComponent", "getComponents", "setComponentEnabled", "createNode", "destroyNode",
            "createPrefab", "spawnPrefab"
        };
        for (auto name : unsupported)
            JS_SetPropertyStr(ctx, target, name, JS_NewCFunction(ctx,
                [](JSContext *context, JSValueConst, int, JSValueConst *) -> JSValue {
                    return JS_ThrowReferenceError(context, "This vb API is not supported by the active game");
                }, name, 0));
    }
}
static void vbjs_define_cvar(JSContext *ctx, JSValue target)
{
    JS_SetPropertyStr(ctx, target, "run", JS_NewCFunction(ctx, js_cvar_run, "run", 1));
    JS_SetPropertyStr(ctx, target, "set", JS_NewCFunction(ctx, js_cvar_set, "set", 2));
}
static void vbjs_define_fs(JSContext *ctx, JSValue target)
{
    JS_SetPropertyStr(ctx, target, "readFile",   JS_NewCFunction(ctx, js_fs_readFile_node,   "readFile", 3));
    JS_SetPropertyStr(ctx, target, "writeFile",  JS_NewCFunction(ctx, js_fs_writeFile_node,  "writeFile", 4));
    JS_SetPropertyStr(ctx, target, "appendFile", JS_NewCFunction(ctx, js_fs_appendFile_node, "appendFile", 4));
    JS_SetPropertyStr(ctx, target, "exists",     JS_NewCFunction(ctx, js_fs_exists_node,     "exists", 2));
    JS_SetPropertyStr(ctx, target, "unlink", JS_NewCFunction(ctx, js_fs_unlink_node, "unlink", 2));
    JS_SetPropertyStr(ctx, target, "rename", JS_NewCFunction(ctx, js_fs_rename_node, "rename", 3));
    JS_SetPropertyStr(ctx, target, "readFileSync", JS_NewCFunction(ctx, js_fs_readFileSync_node, "readFileSync", 2));
    JS_SetPropertyStr(ctx, target, "writeFileSync", JS_NewCFunction(ctx, js_fs_writeFile, "writeFileSync", 2));
    JS_SetPropertyStr(ctx, target, "appendFileSync", JS_NewCFunction(ctx, js_fs_appendFile, "appendFileSync", 2));
    JS_SetPropertyStr(ctx, target, "existsSync", JS_NewCFunction(ctx, js_fs_exists, "existsSync", 1));
    JS_SetPropertyStr(ctx, target, "unlinkSync", JS_NewCFunction(ctx, js_fs_unlinkSync, "unlinkSync", 1));
    JS_SetPropertyStr(ctx, target, "renameSync", JS_NewCFunction(ctx, js_fs_renameSync, "renameSync", 2));
    JS_SetPropertyStr(ctx, target, "mkdirSync", JS_NewCFunction(ctx, js_fs_mkdirSync, "mkdirSync", 2));
    JS_SetPropertyStr(ctx, target, "readdirSync", JS_NewCFunction(ctx, js_fs_readdirSync, "readdirSync", 2));
    JS_SetPropertyStr(ctx, target, "statSync", JS_NewCFunction(ctx, js_fs_statSync, "statSync", 2));
    JSValue promises = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, promises, "readFile", JS_NewCFunction(ctx, js_fs_promises_readFile, "readFile", 1));
    JS_SetPropertyStr(ctx, promises, "writeFile", JS_NewCFunction(ctx, js_fs_promises_writeFile, "writeFile", 2));
    JS_SetPropertyStr(ctx, promises, "appendFile", JS_NewCFunction(ctx, js_fs_promises_appendFile, "appendFile", 2));
    JS_SetPropertyStr(ctx, promises, "exists", JS_NewCFunction(ctx, js_fs_promises_exists, "exists", 1));
    JS_SetPropertyStr(ctx, promises, "unlink", JS_NewCFunction(ctx, js_fs_promises_unlink, "unlink", 1));
    JS_SetPropertyStr(ctx, promises, "rename", JS_NewCFunction(ctx, js_fs_promises_rename, "rename", 2));
    JS_SetPropertyStr(ctx, target, "promises", promises);
}
static void vbjs_define_dgram(JSContext *ctx, JSValue target)
{
    JS_SetPropertyStr(ctx, target, "createSocket", JS_NewCFunction(ctx, js_dgram_createSocket, "createSocket", 2));
}
static void vbjs_define_net(JSContext *ctx, JSValue target)
{
    JSValue connect = JS_NewCFunction(ctx, js_net_createConnection, "createConnection", 3);
    JS_SetPropertyStr(ctx, target, "createConnection", JS_DupValue(ctx, connect));
    JS_SetPropertyStr(ctx, target, "connect", connect);
}

// ES-module init for `import ... from 'vb'` / 'fs' (default + named exports).
static const char *k_vb_exports[] = {
    "on","off","log","drawText","clearText","breakpoint","spawn","A_Spawn","A_InsertSprite","sendMessage","getGamevar","setGamevar","player","session","setMenuText","setMenuBackground","currentMenu","openMenu","setMenuRoute","menuSelection","setMenuSelection","run","define",
    "sound","shoot","addAmmo","addWeapon",
    "getActor","setActor","getPlayer","setPlayer","getInput","setInput","getSector","setSector","getWall","setWall",
    "getProjectile","setProjectile","getActiveProjectile","setActiveProjectile","getTileData","setTileData","getUserdef","setUserdef",
    "VBComponent","VBNode","VBPrefab","attach","detach","getComponent","getComponents","setComponentEnabled","createNode","destroyNode","createPrefab","spawnPrefab",
};
static const char *k_fs_exports[] = { "readFile","writeFile","appendFile","exists","unlink","rename","readFileSync","writeFileSync","appendFileSync","existsSync","unlinkSync","renameSync","mkdirSync","readdirSync","statSync","promises" };
static const char *k_fs_promises_exports[] = { "readFile","writeFile","appendFile","exists","unlink","rename" };
static const char *k_dgram_exports[] = { "createSocket" };
static const char *k_net_exports[] = { "createConnection", "connect" };
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
static int dgram_module_init(JSContext *ctx, JSModuleDef *m)
{
    JSValue o = JS_NewObject(ctx); vbjs_define_dgram(ctx, o);
    for (auto n : k_dgram_exports) JS_SetModuleExport(ctx, m, n, JS_GetPropertyStr(ctx, o, n));
    JS_SetModuleExport(ctx, m, "default", o);
    return 0;
}
static int net_module_init(JSContext *ctx, JSModuleDef *m)
{
    JSValue o = JS_NewObject(ctx); vbjs_define_net(ctx, o);
    for (auto n : k_net_exports) JS_SetModuleExport(ctx, m, n, JS_GetPropertyStr(ctx, o, n));
    JS_SetModuleExport(ctx, m, "default", o);
    return 0;
}
static int fs_promises_module_init(JSContext *ctx, JSModuleDef *m)
{
    JSValue fs = JS_NewObject(ctx); vbjs_define_fs(ctx, fs);
    JSValue promises = JS_GetPropertyStr(ctx, fs, "promises");
    for (auto n : k_fs_promises_exports) JS_SetModuleExport(ctx, m, n, JS_GetPropertyStr(ctx, promises, n));
    JS_SetModuleExport(ctx, m, "default", promises);
    JS_FreeValue(ctx, fs);
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
    if (!strcmp(name, "vb") || !strcmp(name, "fs") || !strcmp(name, "node:fs") || !strcmp(name, "fs/promises") || !strcmp(name, "node:fs/promises") || !strcmp(name, "dgram") || !strcmp(name, "node:dgram") || !strcmp(name, "net") || !strcmp(name, "node:net") || !strcmp(name, "cvar"))
    {
        JSValue o = JS_NewObject(ctx);
        if (!strcmp(name, "vb")) vbjs_define_vb(ctx, o);
        else if (!strcmp(name, "fs") || !strcmp(name, "node:fs")) vbjs_define_fs(ctx, o);
        else if (!strcmp(name, "fs/promises") || !strcmp(name, "node:fs/promises")) { vbjs_define_fs(ctx, o); JSValue promises = JS_GetPropertyStr(ctx, o, "promises"); JS_FreeValue(ctx, o); o = promises; }
        else if (!strcmp(name, "dgram") || !strcmp(name, "node:dgram")) vbjs_define_dgram(ctx, o);
        else if (!strcmp(name, "net") || !strcmp(name, "node:net")) vbjs_define_net(ctx, o);
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
    if (JS_IsException(fn)) return fn;

    JSValue module  = JS_NewObject(ctx);
    JSValue exports  = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, module, "exports", JS_DupValue(ctx, exports));
    JSValue reqfn = JS_NewCFunction(ctx, js_require, "require", 1);
    JSValueConst args[3] = { exports, module, reqfn };
    JSValue r = JS_Call(ctx, fn, JS_UNDEFINED, 3, args);
    JS_FreeValue(ctx, fn); JS_FreeValue(ctx, reqfn);
    if (JS_IsException(r)) { JS_FreeValue(ctx, exports); JS_FreeValue(ctx, module); return r; }
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
    if (!strcmp(name, "vb") || !strcmp(name, "fs") || !strcmp(name, "node:fs") || !strcmp(name, "fs/promises") || !strcmp(name, "node:fs/promises") || !strcmp(name, "dgram") || !strcmp(name, "node:dgram") || !strcmp(name, "net") || !strcmp(name, "node:net") || !strcmp(name, "cvar"))
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

    JS_NewClassID(s_rt, &s_dgramSocketClass);
    JSClassDef dgramSocketClass {};
    dgramSocketClass.class_name = "Socket";
    dgramSocketClass.finalizer = js_dgram_socket_finalizer;
    JS_NewClass(s_rt, s_dgramSocketClass, &dgramSocketClass);
    JSValue dgramPrototype = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, dgramPrototype, "on", JS_NewCFunction(ctx, js_dgram_on, "on", 2));
    JS_SetPropertyStr(ctx, dgramPrototype, "bind", JS_NewCFunction(ctx, js_dgram_bind, "bind", 3));
    JS_SetPropertyStr(ctx, dgramPrototype, "send", JS_NewCFunction(ctx, js_dgram_send, "send", 4));
    JS_SetPropertyStr(ctx, dgramPrototype, "close", JS_NewCFunction(ctx, js_dgram_close, "close", 1));
    JS_SetPropertyStr(ctx, dgramPrototype, "address", JS_NewCFunction(ctx, js_dgram_address, "address", 0));
    JS_SetClassProto(ctx, s_dgramSocketClass, dgramPrototype);
    JS_NewClassID(s_rt, &s_netSocketClass);
    JSClassDef netSocketClass {}; netSocketClass.class_name = "Socket"; netSocketClass.finalizer = js_net_socket_finalizer;
    JS_NewClass(s_rt, s_netSocketClass, &netSocketClass);
    JSValue netPrototype = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, netPrototype, "on", JS_NewCFunction(ctx, js_net_on, "on", 2));
    JS_SetPropertyStr(ctx, netPrototype, "write", JS_NewCFunction(ctx, js_net_write, "write", 2));
    JS_SetPropertyStr(ctx, netPrototype, "end", JS_NewCFunction(ctx, js_net_end, "end", 2));
    JS_SetPropertyStr(ctx, netPrototype, "destroy", JS_NewCFunction(ctx, js_net_destroy, "destroy", 0));
    JS_SetPropertyStr(ctx, netPrototype, "setNoDelay", JS_NewCFunction(ctx, js_net_setNoDelay, "setNoDelay", 1));
    JS_SetClassProto(ctx, s_netSocketClass, netPrototype);

    JSValue g = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, g, "fetch", JS_NewCFunction(ctx, js_fetch, "fetch", 2));

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
    vbjs_setup_components(ctx);
    // The global object was created before the component kernel.  Decorate it
    // once the kernel has supplied the shared class/function identities.
    JSValue globalVb = JS_GetPropertyStr(ctx, g, "vb");
    vbjs_define_vb(ctx, globalVb);
    JS_FreeValue(ctx, globalVb);
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
    JSModuleDef *mnodefs = JS_NewCModule(ctx, "node:fs", fs_module_init);
    for (auto n : k_fs_exports) JS_AddModuleExport(ctx, mnodefs, n);
    JS_AddModuleExport(ctx, mnodefs, "default");
    JSModuleDef *mfsp = JS_NewCModule(ctx, "fs/promises", fs_promises_module_init);
    for (auto n : k_fs_promises_exports) JS_AddModuleExport(ctx, mfsp, n);
    JS_AddModuleExport(ctx, mfsp, "default");
    JSModuleDef *mnodefsp = JS_NewCModule(ctx, "node:fs/promises", fs_promises_module_init);
    for (auto n : k_fs_promises_exports) JS_AddModuleExport(ctx, mnodefsp, n);
    JS_AddModuleExport(ctx, mnodefsp, "default");
    JSModuleDef *mdgram = JS_NewCModule(ctx, "dgram", dgram_module_init);
    for (auto n : k_dgram_exports) JS_AddModuleExport(ctx, mdgram, n);
    JS_AddModuleExport(ctx, mdgram, "default");
    JSModuleDef *mnodedgram = JS_NewCModule(ctx, "node:dgram", dgram_module_init);
    for (auto n : k_dgram_exports) JS_AddModuleExport(ctx, mnodedgram, n);
    JS_AddModuleExport(ctx, mnodedgram, "default");
    JSModuleDef *mnet = JS_NewCModule(ctx, "net", net_module_init);
    for (auto n : k_net_exports) JS_AddModuleExport(ctx, mnet, n);
    JS_AddModuleExport(ctx, mnet, "default");
    JSModuleDef *mnodenet = JS_NewCModule(ctx, "node:net", net_module_init);
    for (auto n : k_net_exports) JS_AddModuleExport(ctx, mnodenet, n);
    JS_AddModuleExport(ctx, mnodenet, "default");
    JSModuleDef *mcvar = JS_NewCModule(ctx, "cvar", cvar_module_init);
    for (auto n : k_cvar_exports) JS_AddModuleExport(ctx, mcvar, n);
    JS_AddModuleExport(ctx, mcvar, "default");
}

// ------------------------------------------------------------- public C API ---

extern "C" {

int VBJS_IsEnabled(void) { return s_enabled; }

void VBJS_CaptureLaunchDir(void) { if (!s_launchDir[0]) getcwd(s_launchDir, sizeof(s_launchDir)); }

void VBJS_SetExecutablePath(const char *path)
{
    if (!path || !path[0] || s_exeDir[0]) return;

    char full[2048] = {};
#ifdef _WIN32
    if (_fullpath(full, path, sizeof(full)) == NULL)
#else
    if (realpath(path, full) == NULL)
#endif
    {
        if (path[0] == '/')
            snprintf(full, sizeof(full), "%s", path);
        else if (strchr(path, '/'))
        {
            char cwd[1024];
            if (getcwd(cwd, sizeof(cwd)))
                snprintf(full, sizeof(full), "%s/%s", cwd, path);
        }
    }

    if (!full[0]) return;
    char *slash = strrchr(full, '/');
#ifdef _WIN32
    char *backslash = strrchr(full, '\\');
    if (!slash || (backslash && backslash > slash)) slash = backslash;
#endif
    if (!slash) return;

    if (slash == full)
        Bstrncpyz(s_exeDir, "/", sizeof(s_exeDir));
    else
    {
        *slash = 0;
        Bstrncpyz(s_exeDir, full, sizeof(s_exeDir));
    }
}

void VBJS_SetGenericMode(int enabled) { s_genericMode = enabled != 0; }

void VBJS_SetScriptPath(const char *path) { vbjs_add_script(path); }

void VBJS_SetDevtools(int enabled) { s_devtools = enabled != 0; if (s_devtools) s_debug = 1; }
int VBJS_DevtoolsEnabled(void) { return s_devtools; }
void VBJS_SetDebug(int enabled) { s_debug = enabled != 0; }
int VBJS_DebugEnabled(void) { return s_debug; }
void VBJS_SetDebugPort(int port) { if (port > 0 && port <= 65535) s_debugPort = port; }
int VBJS_DebugPort(void) { return s_debugPort; }
void VBJS_DevtoolsPump(void)
{
    if (!s_ctx || !s_debuggerWorkPending.exchange(false, std::memory_order_acq_rel)) return;
    js_debugger_cooperate(s_ctx);
    // QuickJS processes one protocol message per cooperate call. Keep the
    // existing pacing when a client queued multiple requests, but stay idle
    // on the common no-debugger path.
    if (vbjs_debug_peek(&s_debugTransport)) s_debuggerWorkPending.store(true, std::memory_order_release);
}
int VBJS_DevtoolsDebuggerSend(const char *json)
{
    if (!json || !json[0]) return 0;
    size_t length = strlen(json); char header[10]; snprintf(header, sizeof(header), "%08x\n", (unsigned)(length + 1));
    pthread_mutex_lock(&s_debugTransport.mutex);
    for (int i = 0; i < 9; ++i) s_debugTransport.inbound.push_back(header[i]);
    for (size_t i = 0; i < length; ++i) s_debugTransport.inbound.push_back(json[i]);
    s_debugTransport.inbound.push_back('\n'); pthread_cond_broadcast(&s_debugTransport.changed); pthread_mutex_unlock(&s_debugTransport.mutex);
    s_debuggerWorkPending.store(true, std::memory_order_release);
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
        "// Optional debug-only launch args, for example:\n"
        "// /// vbd.params=\"-gm duke -g ~/games/duke3da/duke3d.grp -sc\"\n"
        "import { on, log } from 'vb';\n\n"
        "let frames = 0;\n"
        "on('start', () => {\n"
        "  log('TypeScript mod loaded.');\n"
        "});\n\n"
        "on('frame', () => {\n"
        "  const breakpointProbe = `TypeScript frame ${++frames}`;\n"
        "  if (frames % 60 === 0) log(breakpointProbe);\n"
        "});\n";
    const char *indexJs =
        "// VibeBuild32 auto-loads only index.js (or index.ts) from this project.\n"
        "// Optional debug-only launch args, for example:\n"
        "// /// vbd.params=\"-gm duke -g ~/games/duke3da/duke3d.grp -sc\"\n"
        "// @ts-check\n"
        "/** @type {typeof import('vb').default} */\n"
        "const vb = require('vb');\n"
        "\n"
        "let frames = 0;\n"
        "vb.on('start', () => {\n"
        "  vb.log('JavaScript mod loaded.');\n"
        "});\n\n"
        "vb.on('frame', () => {\n"
        "  const breakpointProbe = `JavaScript frame ${++frames}`;\n"
        "  if (frames % 60 === 0) vb.log(breakpointProbe);\n"
        "});\n";
    const char *jsconfig =
        "{\n"
        "  \"compilerOptions\": { \"target\": \"ES2020\", \"lib\": [\"ES2020\"], \"checkJs\": true, \"allowJs\": true, \"noEmit\": true, \"strict\": true },\n"
        "  \"include\": [\"*.js\", \"vb.d.ts\"]\n"
        "}\n";
    const char *types =
        "// VibeBuild32 embedded JavaScript API declarations.\n"
        "declare type ActorEvent = { sprite: number; owner: number; tile: number };\n"
        "declare type VBVector3 = { x: number; y: number; z: number };\n"
        "declare type VBTransform = { position: VBVector3; rotation: VBVector3; scale: VBVector3 };\n"
        "declare class VBNode { readonly id: number; name: string; tag: string; readonly destroyed: boolean; readonly activeSelf: boolean; readonly activeInHierarchy: boolean; transform: VBTransform; SetActive(active: boolean): boolean; AddComponent<T extends VBComponent>(type: VBComponentConstructor<T>): T; GetComponent<T extends VBComponent>(type: VBComponentConstructor<T>): T | undefined; GetComponents<T extends VBComponent>(type?: VBComponentConstructor<T>): T[]; Destroy(): boolean; }\n"
        "declare abstract class VBComponent { sprite: number; node: VBNode | null; readonly gameObject: VBNode | null; readonly transform: VBTransform | undefined; enabled: boolean; readonly isActiveAndEnabled: boolean; GetComponent<T extends VBComponent>(type: VBComponentConstructor<T>): T | undefined; GetComponents<T extends VBComponent>(type?: VBComponentConstructor<T>): T[]; AddComponent<T extends VBComponent>(type: VBComponentConstructor<T>): T | undefined; Destroy(): boolean; awake(): void; start(): void; onSpawn(): void; fixedUpdate(dt: number): void; update(dt: number): void; lateUpdate(dt: number): void; onDestroy(): void; onEnable(): void; onDisable(): void; }\n"
        "declare type VBComponentConstructor<T extends VBComponent = VBComponent> = new () => T;\n"
        "declare class VBPrefab { readonly tile: number; readonly components: readonly VBComponentConstructor[]; }\n"
        "declare type ChatEvent = { player: number; text: string };\n"
        "declare type MenuEvent = { menu: number; previousMenu?: number };\n"
        "declare type ConEvent = { sprite: number; player: number; dist: number };\n"
        "declare type Player = { sprite: number; x: number; y: number; z: number };\n"
        "declare type SessionPlayer = { index: number; name: string; sprite: number; isLocal: boolean; isHost: boolean };\n"
        "declare type GameSession = { started: boolean; skill: number; episode: number; level: number; episodeName?: string; levelName?: string; userMap?: string; localPlayer: number; hostPlayer?: number; hostName?: string; serverAddress?: string; players: SessionPlayer[]; };\n"
        "declare type ConValue = number | undefined;\n"
        "declare interface VBConsole { log(...args: unknown[]): void; error(...args: unknown[]): void; warn(...args: unknown[]): void; info(...args: unknown[]): void; }\n"
        "declare interface VBHeaders { get(name: string): string | null; }\n"
        "declare interface VBFetchResponse { readonly status: number; readonly ok: boolean; readonly url: string; readonly headers: VBHeaders; text(): Promise<string>; json<T = unknown>(): Promise<T>; arrayBuffer(): Promise<ArrayBuffer>; }\n"
        "declare interface VBFetchOptions { method?: string; body?: string | Uint8Array; headers?: Record<string, string>; }\n"
        "declare function fetch(url: string, options?: VBFetchOptions): Promise<VBFetchResponse>;\n"
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
        "declare type VBLifecycleEventName = 'start' | 'shutdown' | 'frame' | 'playerDied' | 'gameStarted' | 'levelChanged' | 'levelFinished' | 'leftGame' | 'beforeQuit';\n"
        "declare type VBEventName = VBLifecycleEventName | 'menuOpened' | 'menuClosed' | 'menuChanged' | 'escapeMenuOpened' | 'actorSpawned' | 'actorDestroyed' | 'chatMessage' | ConEventName;\n"
        "declare type VBEventMap = {\n"
        "  start: void; shutdown: void; frame: void; playerDied: void; gameStarted: void; levelChanged: void; levelFinished: void; leftGame: void; beforeQuit: void;\n"
        "  menuOpened: MenuEvent; menuClosed: MenuEvent; menuChanged: MenuEvent; escapeMenuOpened: MenuEvent;\n"
        "  actorSpawned: ActorEvent; actorDestroyed: ActorEvent; chatMessage: ChatEvent;\n"
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
        "  VBComponent: typeof VBComponent; VBNode: typeof VBNode; VBPrefab: typeof VBPrefab; attach<T extends VBComponent>(entity: number | VBNode, component: T | VBComponentConstructor<T>): T; detach(entity: number | VBNode, component: VBComponent): boolean; getComponent<T extends VBComponent>(entity: number | VBNode, type: VBComponentConstructor<T>): T | undefined; getComponents<T extends VBComponent>(entity: number | VBNode, type?: VBComponentConstructor<T>): T[]; setComponentEnabled(component: VBComponent, enabled: boolean): void; createNode(...components: VBComponentConstructor[]): VBNode; destroyNode(node: VBNode): boolean; createPrefab(tile: number, ...components: VBComponentConstructor[]): VBPrefab; spawnPrefab(prefab: VBPrefab, x?: number, y?: number, z?: number, owner?: number): number;\n"
        "  sendMessage(text: string): void; player(): Player; session(): GameSession; setMenuText(original: string, replacement: string): void; setMenuBackground(tile: number | null): void; currentMenu(): number; openMenu(menuId: number): void; setMenuRoute(menuId: number, itemIndex: number, destination: number | null): void; menuSelection(): number; setMenuSelection(index: number): void;\n"
        "  getGamevar(name: string, sprite?: number, player?: number): ConValue; setGamevar(name: string, value: number, sprite?: number, player?: number): void;\n"
        "  getActor(i: number, member: string, parm2?: number): ConValue; setActor(i: number, member: string, value: number, parm2?: number): void;\n"
        "  getPlayer(i: number, member: string, parm2?: number): ConValue; setPlayer(i: number, member: string, value: number, parm2?: number): void;\n"
        "  getInput(i: number, member: string): ConValue; setInput(i: number, member: string, value: number): void;\n"
        "  getSector(i: number, member: string, parm2?: number): ConValue; setSector(i: number, member: string, value: number, parm2?: number): void; getWall(i: number, member: string, parm2?: number): ConValue; setWall(i: number, member: string, value: number, parm2?: number): void;\n"
        "  getProjectile(i: number, member: string, parm2?: number): ConValue; setProjectile(i: number, member: string, value: number, parm2?: number): void; getActiveProjectile(i: number, member: string, parm2?: number): ConValue; setActiveProjectile(i: number, member: string, value: number, parm2?: number): void; getTileData(i: number, member: string, parm2?: number): ConValue; setTileData(i: number, member: string, value: number, parm2?: number): void;\n"
        "  getUserdef(member: string, parm2?: number): ConValue; setUserdef(member: string, value: number, parm2?: number): void; define(name: string): ConValue;\n"
        "  sound(num: number, sprite?: number): void; shoot(sprite: number, tile: number): number; addAmmo(player: number, weapon: number, amount: number): void; addWeapon(player: number, weapon: number, switchTo?: number): void; run(command: string): void;\n"
        "}\n"
        "declare interface FSPromises { readFile(path: string, options?: unknown): Promise<string | Uint8Array>; writeFile(path: string, data: string, options?: unknown): Promise<void>; appendFile(path: string, data: string, options?: unknown): Promise<void>; exists(path: string): Promise<boolean>; unlink(path: string): Promise<void>; rename(oldPath: string, newPath: string): Promise<void>; }\n"
        "declare interface FS { readFile(path: string, options?: unknown): string | Uint8Array; readFile(path: string, options: unknown, callback: (error: Error | null, data?: string | Uint8Array) => void): void; readFile(path: string, callback: (error: Error | null, data?: string | Uint8Array) => void): void; writeFile(path: string, data: string): void; writeFile(path: string, data: string, options: unknown, callback: (error: Error | null) => void): void; writeFile(path: string, data: string, callback: (error: Error | null) => void): void; appendFile(path: string, data: string): void; appendFile(path: string, data: string, options: unknown, callback: (error: Error | null) => void): void; appendFile(path: string, data: string, callback: (error: Error | null) => void): void; exists(path: string): boolean; exists(path: string, callback: (exists: boolean) => void): void; readFileSync(path: string, options?: unknown): string | Uint8Array; writeFileSync(path: string, data: string, options?: unknown): void; appendFileSync(path: string, data: string, options?: unknown): void; existsSync(path: string): boolean; readonly promises: FSPromises; }\n"
        "declare interface CVar { run(command: string): void; set(name: string, value: unknown): void; }\n"
        "declare const console: VBConsole; declare const vb: VB; declare const cvar: CVar;\n"
        "declare interface DgramRemoteInfo { address: string; family: 'IPv4' | 'IPv6'; port: number; size: number; }\n"
        "declare interface DgramSocket { on(event: 'message', listener: (message: Uint8Array, rinfo: DgramRemoteInfo) => void): this; on(event: 'error', listener: (error: Error) => void): this; on(event: 'listening' | 'close', listener: () => void): this; bind(port: number, callback?: () => void): this; bind(port: number, address: string, callback?: () => void): this; send(message: string | Uint8Array, port: number, callback?: (error: Error | null) => void): void; send(message: string | Uint8Array, port: number, address: string, callback?: (error: Error | null) => void): void; close(callback?: () => void): void; address(): { address: string; family: 'IPv4' | 'IPv6'; port: number }; }\n"
        "declare interface Dgram { createSocket(type: 'udp4' | 'udp6', callback?: (message: Uint8Array, rinfo: DgramRemoteInfo) => void): DgramSocket; }\n"
        "declare interface NetSocket { on(event: 'connect' | 'end', listener: () => void): this; on(event: 'data', listener: (data: Uint8Array) => void): this; on(event: 'error', listener: (error: Error) => void): this; on(event: 'close', listener: (hadError: boolean) => void): this; write(data: string | Uint8Array, callback?: () => void): boolean; end(data?: string | Uint8Array, callback?: () => void): this; destroy(): this; setNoDelay(noDelay?: boolean): this; }\n"
        "declare interface Net { createConnection(port: number, callback?: () => void): NetSocket; createConnection(port: number, host: string, callback?: () => void): NetSocket; connect: Net['createConnection']; }\n"
        "declare function require(name: 'vb'): VB; declare function require(name: 'fs'): FS; declare function require(name: 'dgram'): Dgram; declare function require(name: 'net'): Net; declare function require(name: 'cvar'): CVar; declare function require(name: string): unknown;\n"
        "declare module 'vb' { export { VBComponent, VBNode, VBPrefab }; export type { ActorEvent, ChatEvent, ConEvent, ConEventName, ConEventShortName, ConValue, Player, VBComponentConstructor, VBEventMap, VBEventName, VBLifecycleEventName, VBConsole, VBTransform, VBVector3 }; const api: VB; export default api; export const on: VB['on']; export const off: VB['off']; export const log: VB['log']; export const drawText: VB['drawText']; export const clearText: VB['clearText']; export const breakpoint: VB['breakpoint']; export const spawn: VB['spawn']; export const A_Spawn: VB['A_Spawn']; export const A_InsertSprite: VB['A_InsertSprite']; export const attach: VB['attach']; export const detach: VB['detach']; export const getComponent: VB['getComponent']; export const getComponents: VB['getComponents']; export const setComponentEnabled: VB['setComponentEnabled']; export const createNode: VB['createNode']; export const destroyNode: VB['destroyNode']; export const createPrefab: VB['createPrefab']; export const spawnPrefab: VB['spawnPrefab']; export const sendMessage: VB['sendMessage']; export const player: VB['player']; export const getGamevar: VB['getGamevar']; export const setGamevar: VB['setGamevar']; export const run: VB['run']; export const define: VB['define']; export const sound: VB['sound']; export const shoot: VB['shoot']; export const addAmmo: VB['addAmmo']; export const addWeapon: VB['addWeapon']; export const getActor: VB['getActor']; export const setActor: VB['setActor']; export const getPlayer: VB['getPlayer']; export const setPlayer: VB['setPlayer']; export const getInput: VB['getInput']; export const setInput: VB['setInput']; export const getSector: VB['getSector']; export const setSector: VB['setSector']; export const getWall: VB['getWall']; export const setWall: VB['setWall']; export const getProjectile: VB['getProjectile']; export const setProjectile: VB['setProjectile']; export const getActiveProjectile: VB['getActiveProjectile']; export const setActiveProjectile: VB['setActiveProjectile']; export const getTileData: VB['getTileData']; export const setTileData: VB['setTileData']; export const getUserdef: VB['getUserdef']; export const setUserdef: VB['setUserdef']; }\n"
        "declare module 'vb' { export type { SessionPlayer, GameSession, MenuEvent }; export const session: VB['session']; export const setMenuText: VB['setMenuText']; export const setMenuBackground: VB['setMenuBackground']; export const currentMenu: VB['currentMenu']; export const openMenu: VB['openMenu']; export const setMenuRoute: VB['setMenuRoute']; export const menuSelection: VB['menuSelection']; export const setMenuSelection: VB['setMenuSelection']; }\n"
        "declare module 'fs' { export type { FSPromises }; const api: FS; export default api; export const readFile: FS['readFile']; export const writeFile: FS['writeFile']; export const appendFile: FS['appendFile']; export const exists: FS['exists']; export const readFileSync: FS['readFileSync']; export const writeFileSync: FS['writeFileSync']; export const appendFileSync: FS['appendFileSync']; export const existsSync: FS['existsSync']; export const promises: FS['promises']; }\n"
        "declare module 'node:fs' { export * from 'fs'; import api from 'fs'; export default api; }\n"
        "declare module 'fs/promises' { import api from 'fs'; const promises: FSPromises; export default promises; export const readFile: FSPromises['readFile']; export const writeFile: FSPromises['writeFile']; export const appendFile: FSPromises['appendFile']; export const exists: FSPromises['exists']; export const unlink: FSPromises['unlink']; export const rename: FSPromises['rename']; }\n"
        "declare module 'node:fs/promises' { export * from 'fs/promises'; import api from 'fs/promises'; export default api; }\n"
        "declare module 'dgram' { export type { DgramRemoteInfo, DgramSocket }; const api: Dgram; export default api; export const createSocket: Dgram['createSocket']; }\n"
        "declare module 'node:dgram' { export * from 'dgram'; import api from 'dgram'; export default api; }\n"
        "declare module 'net' { export type { NetSocket }; const api: Net; export default api; export const createConnection: Net['createConnection']; export const connect: Net['connect']; }\n"
        "declare module 'node:net' { export * from 'net'; import api from 'net'; export default api; }\n"
        "declare module 'fs' { export const unlink: (path: string, callback?: (error: Error | null) => void) => void; export const rename: (oldPath: string, newPath: string, callback?: (error: Error | null) => void) => void; export const unlinkSync: (path: string) => void; export const renameSync: (oldPath: string, newPath: string) => void; }\n"
        "declare interface FS { unlink(path: string, callback?: (error: Error | null) => void): void; rename(oldPath: string, newPath: string, callback?: (error: Error | null) => void): void; unlinkSync(path: string): void; renameSync(oldPath: string, newPath: string): void; }\n"
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
    // The adapter is a single-use background task.  It exits when the DAP
    // client or the game exits, so a later VS Code launch never attaches to
    // stale state from an earlier run.
    const char *vscodeFiles[] = { "launch.json", "tasks.json", "README.md", "vibebuild32-debug-adapter.js", "vibebuild32-start-debug-adapter.js" };
    for (auto file : vscodeFiles)
    {
        char target[1400]; size_t length = 0;
        char *contents = vbjs_read_vscode_template(file, &length);
        if (!contents) return -1;
        snprintf(target, sizeof(target), "%s/%s", vscode, file);
        int failed = vbjs_write_text(target, contents); free(contents);
        if (failed) return -1;
    }
    fprintf(stdout, "Created %s project: %s\n", typescript ? "TypeScript" : "JavaScript", project);
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

    s_fetchShuttingDown = false;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    s_rt = JS_NewRuntime();
    if (!s_rt) { vbjs_log("[js] failed to create runtime"); return; }
    JS_SetHostPromiseRejectionTracker(s_rt, vbjs_promise_rejection_tracker, nullptr);
    s_ctx = JS_NewContext(s_rt);
    if (!s_ctx) { JS_FreeRuntime(s_rt); s_rt = nullptr; vbjs_log("[js] failed to create context"); return; }

    vbjs_setup_context(s_ctx);
    // -dbg exposes the localhost attach endpoint. -dt implies it and also
    // opens the visual inspector; ordinary mods incur no debugger overhead.
    if (s_debug)
    {
        pthread_mutex_lock(&s_debugTransport.mutex);
        s_debugTransport.closed = 0; s_debugTransport.inbound.clear(); s_debugTransport.outbound[0].clear(); s_debugTransport.outbound[1].clear();
        pthread_mutex_unlock(&s_debugTransport.mutex);
        s_debuggerWorkPending.store(false, std::memory_order_release);
        js_debugger_attach(s_ctx, vbjs_debug_read, vbjs_debug_write, vbjs_debug_peek, vbjs_debug_close, &s_debugTransport);
        VBJS_DevtoolsStartDebuggerBridge();
    }
    // VS Code has to attach and submit breakpoints after the runtime starts.
    // Hold both top-level module evaluation and the one-shot `start` callback
    // until then, so breakpoints work everywhere in a freshly launched mod.
    s_scriptsLoaded = 0;
    s_frameEventListened = 0;
    s_observedMenuOpen = -1;
    s_observedMenuId = -1;
    s_menuTextOverrides.clear();
    s_menuRoutes.clear();
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
        if (!src) { char m[1100]; snprintf(m, sizeof(m), "could not read %s", path); vbjs_log(m); continue; }

        JSValue r = JS_Eval(s_ctx, src, len, path, JS_EVAL_TYPE_MODULE);   // module so import/export work
        free(src);
        if (JS_IsException(r))
        {
            char context[1200];
            snprintf(context, sizeof(context), "failed to load %s", path);
            vbjs_dump_error(s_ctx, context);
        }
        else
        {
            char m[1100]; snprintf(m, sizeof(m), "loaded %s", path); vbjs_log(m);
        }
        JS_FreeValue(s_ctx, r);
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
    // Detached libcurl workers must never publish JSValue handles after this
    // runtime has been destroyed. libcurl stays globally initialized until
    // process exit, which is required while those workers unwind.
    s_fetchShuttingDown = true;
    vbjs_fetch_drain(s_ctx);
    for (auto &call : s_fsCallbacks) { JS_FreeValue(s_ctx, call.callback); JS_FreeValue(s_ctx, call.value); }
    s_fsCallbacks.clear();
    JS_FreeValue(s_ctx, s_events);
    JS_FreeValue(s_ctx, s_requireCache);
    JS_FreeValue(s_ctx, s_componentExports); s_componentExports = JS_UNDEFINED;
    JS_FreeValue(s_ctx, s_devtoolsEvent); s_devtoolsEvent = JS_UNDEFINED;
    JS_FreeContext(s_ctx); JS_FreeRuntime(s_rt);
    s_ctx = nullptr; s_rt = nullptr; s_enabled = 0; s_scriptsLoaded = 0; s_startPending = 0; s_frameEventListened = 0; s_gameStartedPending = 0; s_gameSessionActive = 0;
    s_menuTextOverrides.clear(); s_menuRoutes.clear(); s_observedMenuOpen = -1; s_observedMenuId = -1;
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
        // A VS Code client installs source breakpoints before sending
        // configurationDone.  In -dbg/-dt mode, do not race that handshake:
        // top-level statements and one-shot `start` handlers must remain
        // breakable even on a slow editor/task startup.  Non-debug launches
        // retain the short bounded wait used for ordinary auto-loaded mods.
        if (!js_debugger_configuration_done(s_ctx))
        {
            if (s_debug || ++s_startWaitFrames < 30) return;
        }
        s_startPending = 0;
        vbjs_load_scripts();
        VBJS_FireSimple("start");
        if (s_gameStartedPending)
        {
            s_gameStartedPending = 0;
            VBJS_FireSimple("gameStarted");
        }
    }
    // Network events remain live while the visual debugger is paused, just as
    // they do in Node while a debugger is attached.
    vbjs_dgram_poll(s_ctx);
    vbjs_net_poll(s_ctx);
    vbjs_fetch_drain(s_ctx);
    if (s_devtoolsPaused) return;
    if (!s_genericMode)
    {
        int const menuOpen = vbjs_game::menuOpen();
        if (s_observedMenuOpen >= 0 && s_observedMenuOpen != menuOpen)
            VBJS_FireMenu(menuOpen ? "menuOpened" : "menuClosed", vbjs_game::currentMenu(), s_observedMenuId);
        s_observedMenuOpen = menuOpen;
        s_observedMenuId = vbjs_game::currentMenu();
    }
    for (auto &call : s_fsCallbacks)
    {
        JSValue args[2]; int argc = 0;
        if (call.existsStyle) args[argc++] = call.value;
        else { args[argc++] = call.error ? call.value : JS_NULL; if (!call.error) args[argc++] = call.value; }
        JSValue result = JS_Call(s_ctx, call.callback, JS_UNDEFINED, argc, args);
        if (JS_IsException(result)) vbjs_dump_error(s_ctx, "uncaught exception in fs callback");
        JS_FreeValue(s_ctx, result); JS_FreeValue(s_ctx, call.callback); JS_FreeValue(s_ctx, call.value);
    }
    s_fsCallbacks.clear();
    if (s_frameEventListened) VBJS_FireSimple("frame");
    // Promise jobs are uncommon in an idle mod.  JS_ExecutePendingJob() also
    // performs this empty-queue test internally, but avoiding that heavier
    // call on every rendered frame keeps the no-work scripting path minimal.
    if (JS_IsJobPending(s_rt))
    {
        JSContext *c = nullptr;
        int result = 0;
        while ((result = JS_ExecutePendingJob(s_rt, &c)) > 0) {}
        if (result < 0) vbjs_dump_error(c ? c : s_ctx, "unhandled promise rejection");
    }
}

void VBJS_FireSimple(const char *event)
{
    if (event && !strcmp(event, "gameStarted"))
    {
        if (s_gameSessionActive) return;
        s_gameSessionActive = 1;
    }
    else if (event && !strcmp(event, "leftGame"))
        s_gameSessionActive = 0;
    if (!s_ctx)
    {
        if (event && !strcmp(event, "gameStarted")) s_gameStartedPending = 1;
        return;
    }
    vbjs_fire(event, 0, nullptr);
}

void VBJS_FireMenu(const char *event, int menu, int previousMenu)
{
    if (!s_ctx) return;
    JSValue value = JS_NewObject(s_ctx);
    JS_SetPropertyStr(s_ctx, value, "menu", JS_NewInt32(s_ctx, menu));
    JS_SetPropertyStr(s_ctx, value, "previousMenu", previousMenu >= 0 ? JS_NewInt32(s_ctx, previousMenu) : JS_UNDEFINED);
    JSValueConst args[1] = { value };
    vbjs_fire(event, 1, args);
    JS_FreeValue(s_ctx, value);
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
