// NNBuild JS scripting — Duke3D game bindings.
//
// This TU includes the Duke game headers (which clash with quickjs.h, so the
// QuickJS side lives in vbjs.cpp). It implements the vbjs_game::* seam the
// script API calls into, plus the HUD text draw.
#include "duke3d.h"
#include "funct.h"
#include "gamevars.h"
#include "gamedef.h"
#include "gamestructures.h"
#include "global.h"
#include "oldnet.h"
#include "chatpipe.h"
#include "osd.h"
#include "vbjs.h"

#include <deque>
#include <string>

static std::deque<std::string> scriptCommands;

#if SDL_MAJOR_VERSION >= 2 && defined(USE_OPENGL)
# include "imgui.h"
# include "imgui_impl_sdl2.h"
# include "imgui_impl_opengl3.h"
# include "sdl_inc.h"
# include "glbuild.h"
# ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
# else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <fcntl.h>
#  include <unistd.h>
# endif
# include <signal.h>
# include <pthread.h>
# ifdef __APPLE__
extern "C" void VBJS_CocoaMakeKeyWindow(SDL_Window *window);
extern "C" void VBJS_CocoaPlaceGameLeft(SDL_Window *window);
extern "C" void VBJS_CocoaDevtoolsShow(void);
extern "C" void VBJS_CocoaDevtoolsRefresh(void);
extern "C" int VBJS_CocoaDevtoolsProcessEvent(void const *event);
# endif
#endif

extern hashtable_t h_gamevars;   // gamevars.cpp

static int vbjs_playerSprite(void)
{
    if ((unsigned)myconnectindex >= MAXPLAYERS || !g_player[myconnectindex].ps)
        return -1;
    return g_player[myconnectindex].ps->i;
}

static int s_vbjsMenuBackground = -1;

namespace vbjs_game {

int A_Spawn(int spriteNum, int tileNum)
{
    return ::A_Spawn(spriteNum, tileNum);
}

int A_InsertSprite(int whatsect, int s_x, int s_y, int s_z, int s_pn, int s_s,
                   int s_xr, int s_yr, int s_a, int s_ve, int s_zv, int s_ow, int s_ss)
{
    return ::A_InsertSprite(whatsect, s_x, s_y, s_z, s_pn, s_s, s_xr, s_yr, s_a, s_ve, s_zv, s_ow, s_ss);
}

void log(const char *s)
{
    LOG_F(INFO, "[js] %s", s ? s : "");
}

int spawn(int tile, int x, int y, int z, int owner)
{
    if ((unsigned)tile >= MAXTILES) return -1;
    int const psp   = vbjs_playerSprite();
    int const psect = (psp >= 0) ? sprite[psp].sectnum : -1;

    int sect = (owner >= 0 && owner < MAXSPRITES && sprite[owner].sectnum >= 0) ? sprite[owner].sectnum : psect;

    if (x == 0 && y == 0 && z == 0 && psp >= 0)          // no location -> at the player
    {
        x = sprite[psp].x; y = sprite[psp].y; z = sprite[psp].z;
    }
    else if (sect >= 0)                                   // find the sector the point lives in
    {
        int16_t s2 = (int16_t)sect;
        updatesector(x, y, &s2);
        if (s2 >= 0) sect = s2;
    }
    if (sect < 0) return -1;

    int const spawned = A_InsertSprite(sect, x, y, z, tile, 0, 48, 48, 0, 0, 0, (owner >= 0 ? owner : psp), 1);
    if (spawned >= 0)
    {
        // A_InsertSprite only allocates a raw Build sprite. A_Spawn performs
        // the CON actor initialization that gives enemies health, collision,
        // AI, and normal damage handling.
        A_Spawn(-1, spawned);
    }
    return spawned;
}

void sendChat(const char *text)
{
    if (!text || !text[0]) return;
    if (numplayers > 1 && myconnectindex == connecthead)
    {
        Net_ServerMessage(-1, text);
        return;
    }

    // Match the game's normal "send to all" path: format the player name,
    // deliver it locally, write to the optional chat pipe, and route it through
    // the master so every connected player receives the same packet.
    char message[MAXUSERQUOTELEN];
    char packet[MAXUSERQUOTELEN + 2];
    Bsnprintf(message, sizeof(message), "%s^07: %s", g_player[myconnectindex].user_name, text);
    packet[0] = PACKET_TYPE_MESSAGE;
    packet[1] = 255;  // all players
    Bstrncpyz(packet + 2, message, sizeof(packet) - 2);

    G_AddUserQuote(message);
    ChatPipe_SendMessage(text);

    if (ud.multimode > 1)
    {
        int player;
        int const length = (int)Bstrlen(packet + 2) + 2;
        TRAVERSE_CONNECT(player)
        {
            if (player != myconnectindex)
                mmulti_sendpacket(player, (unsigned char *)packet, length);
            // In master/slave mode, clients send only to the master; it relays.
            if (myconnectindex != connecthead) break;
        }
    }
}

bool isServer(void)
{
    return numplayers > 1 && myconnectindex == connecthead && g_networkBroadcastMode == NETMODE_MASTERSLAVE;
}

void sendPrivate(int player, uint32_t connection, const char *text)
{
    if (connection && Net_PlayerConnection(player) == connection)
        Net_ServerMessage(player, text);
}

int getGamevar(const char *name, int spriteNum, int playerNum, int *ok)
{
    int const id = hash_find(&h_gamevars, name);
    if (id < 0) { if (ok) *ok = 0; return 0; }
    if (ok) *ok = 1;
    return Gv_GetVarByLabel(name, 0, spriteNum, playerNum);
}

void setGamevar(const char *name, int value, int spriteNum, int playerNum)
{
    int const id = hash_find(&h_gamevars, name);
    if (id >= 0) Gv_SetVar(id, value, spriteNum, playerNum);
}

int playerSprite(void) { return vbjs_playerSprite(); }

int gameStarted(void) { return (ud.gm & MODE_GAME) != 0; }
int skill(void) { return ud.player_skill + 1; }
int episode(void) { return ud.volume_number + 1; }
int level(void) { return ud.level_number + 1; }
int localPlayer(void) { return (unsigned)myconnectindex < MAXPLAYERS ? myconnectindex : -1; }
int hostPlayer(void) { return (unsigned)connecthead < MAXPLAYERS ? connecthead : -1; }
int playerCount(void) { return max(0, numplayers); }
int playerAt(int ordinal)
{
    if (ordinal < 0 || ordinal >= numplayers) return -1;
    int player = connecthead;
    for (int i = 0; i < ordinal && (unsigned)player < MAXPLAYERS; ++i) player = connectpoint2[player];
    return (unsigned)player < MAXPLAYERS ? player : -1;
}
int playerSpriteAt(int player)
{
    return (unsigned)player < MAXPLAYERS && g_player[player].ps ? g_player[player].ps->i : -1;
}
const char *playerName(int player)
{
    return (unsigned)player < MAXPLAYERS ? g_player[player].user_name : nullptr;
}
const char *episodeName(void)
{
    return (unsigned)ud.volume_number < MAXVOLUMES ? EpisodeNames[ud.volume_number] : nullptr;
}
const char *levelName(void)
{
    int const index = ud.volume_number * MAXLEVELS + ud.level_number;
    return (unsigned)index < (MAXVOLUMES + 1) * MAXLEVELS ? g_mapInfo[index].name : nullptr;
}
const char *userMap(void) { return boardfilename[0] ? boardfilename : nullptr; }
const char *connectedServer(void)
{
    // The join target is retained for lobby/direct connections. A local host
    // has no remote server address, so report undefined instead of inventing one.
    return myconnectindex != connecthead && g_punchTargetAddr[0] ? g_punchTargetAddr : nullptr;
}
void setMenuBackground(int tile) { s_vbjsMenuBackground = (unsigned)tile < MAXTILES ? tile : -1; }
int currentMenu(void) { return g_currentMenu; }
void openMenu(int menu) { ChangeToMenu(menu); }
int menuOpen(void) { return (ud.gm & MODE_MENU) != 0; }
int menuSelection(void) { return probey; }
void setMenuSelection(int selection) { probey = selection; }

void runCommand(const char *cmd)
{
    if (!cmd) return;
    while (*cmd == '/' || *cmd == ' ') cmd++;   // tolerate a leading console-style '/'
    if (*cmd)
        scriptCommands.emplace_back(cmd);
}

} // namespace vbjs_game

extern "C" int VBJS_MenuBackground(void) { return s_vbjsMenuBackground; }

namespace vbjs_game {

// --- generic CON struct access (drives vb.getActor / getPlayer / getSector / ...) ---

static hashtable_t *vbjs_tableFor(int kind)
{
    switch (kind)
    {
        case VBJS_ACTOR:       return &h_actor;
        case VBJS_PLAYER:      return &h_player;
        case VBJS_INPUT:       return &h_input;
        case VBJS_SECTOR:      return &h_sector;
        case VBJS_WALL:        return &h_wall;
        case VBJS_PROJECTILE:  return &h_projectile;
        case VBJS_ACTIVEPROJ:  return &h_projectile;
        case VBJS_TILEDATA:    return &h_tiledata;
        case VBJS_USERDEF:     return &h_userdef;
        default:               return nullptr;
    }
}

int conGet(int kind, int index, const char *member, int parm2, int *ok)
{
    hashtable_t *t = vbjs_tableFor(kind);
    int const label = t ? hash_findcase(t, member) : -1;
    if (label < 0) { if (ok) *ok = 0; return 0; }
    if (ok) *ok = 1;
    switch (kind)
    {
        case VBJS_ACTOR:      return VM_GetSprite(index, label, parm2);
        case VBJS_PLAYER:     return VM_GetPlayer(index, label, parm2);
        case VBJS_INPUT:      return VM_GetPlayerInput(index, label);
        case VBJS_SECTOR:     return VM_GetSector(index, label);
        case VBJS_WALL:       return VM_GetWall(index, label);
        case VBJS_PROJECTILE: return VM_GetProjectile(index, label);
        case VBJS_ACTIVEPROJ: return VM_GetActiveProjectile(index, label);
        case VBJS_TILEDATA:   return VM_GetTileData(index, label);
        case VBJS_USERDEF:    return VM_GetUserdef(label, parm2);
        default:              if (ok) *ok = 0; return 0;
    }
}

void conSet(int kind, int index, const char *member, int value, int parm2)
{
    hashtable_t *t = vbjs_tableFor(kind);
    int const label = t ? hash_findcase(t, member) : -1;
    if (label < 0) return;
    switch (kind)
    {
        case VBJS_ACTOR:      VM_SetSprite(index, label, parm2, value); break;
        case VBJS_PLAYER:     VM_SetPlayer(index, label, parm2, value); break;
        case VBJS_INPUT:      VM_SetPlayerInput(index, label, value); break;
        case VBJS_SECTOR:     VM_SetSector(index, label, value); break;
        case VBJS_WALL:       VM_SetWall(index, label, value); break;
        case VBJS_PROJECTILE: VM_SetProjectile(index, label, value); break;
        case VBJS_ACTIVEPROJ: VM_SetActiveProjectile(index, label, value); break;
        case VBJS_TILEDATA:   VM_SetTileData(index, label, value); break;
        case VBJS_USERDEF:    VM_SetUserdef(label, parm2, value); break;
        default: break;
    }
}

int conDefine(const char *name, int *ok)
{
    for (int id = g_labelCnt - 1; id >= 0; --id)
    {
        if ((labeltype[id] & LABEL_DEFINE) && !Bstrcmp(label + (id << 6), name))
        {
            if (ok)
                *ok = 1;
            return labelcode[id];
        }
    }
    if (ok) *ok = 0;
    return 0;
}

// --- CON action commands ---

void sound(int num, int sprite)
{
    if ((unsigned)sprite < MAXSPRITES) A_PlaySound(num, sprite);
    else                               S_PlaySound(num);
}

int shoot(int sprite, int tile)
{
    if ((unsigned)sprite >= MAXSPRITES) return -1;
    return A_Shoot(sprite, tile);
}

void addAmmo(int playerNum, int weapon, int amount)
{
    if ((unsigned)playerNum < MAXPLAYERS && g_player[playerNum].ps)
        P_AddAmmo(weapon, g_player[playerNum].ps, amount);
}

void addWeapon(int playerNum, int weapon, int switchTo)
{
    if ((unsigned)playerNum < MAXPLAYERS && g_player[playerNum].ps)
        P_AddWeapon(g_player[playerNum].ps, weapon, switchTo);
}

// --- CON event (EVENT_*) name <-> number ---

int eventNum(const char *name)
{
    if (!name) return -1;
    for (int i = 0; i < MAXEVENTS; i++)
        if (EventNames[i] && !Bstrcasecmp(EventNames[i], name))
            return i;
    // also accept the name without the EVENT_ prefix, e.g. "SPAWN"
    if (Bstrncasecmp(name, "EVENT_", 6) != 0)
    {
        char buf[64]; Bsnprintf(buf, sizeof(buf), "EVENT_%s", name);
        for (int i = 0; i < MAXEVENTS; i++)
            if (EventNames[i] && !Bstrcasecmp(EventNames[i], buf))
                return i;
    }
    return -1;
}

const char *eventName(int num)
{
    return ((unsigned)num < MAXEVENTS) ? EventNames[num] : nullptr;
}

void spriteXYZ(int spriteNum, int *x, int *y, int *z, int *ok)
{
    if ((unsigned)spriteNum >= MAXSPRITES) { if (ok) *ok = 0; return; }
    if (x) *x = sprite[spriteNum].x;
    if (y) *y = sprite[spriteNum].y;
    if (z) *z = sprite[spriteNum].z;
    if (ok) *ok = 1;
}

} // namespace vbjs_game

extern "C" void VBJS_DispatchCommands(void)
{
    auto commands = std::move(scriptCommands);
    scriptCommands.clear();
    for (auto const &command : commands)
        OSD_Dispatch(command.c_str());
}

#if SDL_MAJOR_VERSION >= 2 && defined(USE_OPENGL)
static SDL_Window *s_devtoolsWindow = nullptr;
static SDL_GLContext s_devtoolsContext = nullptr;
static ImGuiContext *s_devtoolsImGui = nullptr;
static Uint32 s_devtoolsWindowId = 0;
static int s_devtoolsWindowClosed = 0;

// ---- Debugger bridge: a localhost HTTP listener for the VS Code debug
// adapter (and, on macOS, the browser DevTools). Available on every platform.
static int s_webDebuggerListen = -1;
static int s_webDebuggerPort = 0;
static pthread_t s_webDebuggerThread;
static int s_webDebuggerThreadStarted = 0;

static void vbjs_web_close(int fd)
{
#ifdef _WIN32
    closesocket((SOCKET)fd);
#else
    close(fd);
#endif
}

static void vbjs_web_reply(int fd, char const *body, char const *type = "text/plain; charset=utf-8")
{
    char header[256];
    int const headerLen = snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\nAccess-Control-Allow-Origin: *\r\nContent-Type: %s\r\nContent-Length: %u\r\nConnection: close\r\n\r\n", type, (unsigned)strlen(body));
    send(fd, header, headerLen, 0); send(fd, body, (int)strlen(body), 0);
}

static void vbjs_web_decode(char *dst, size_t dstSize, char const *src)
{
    size_t n = 0;
    while (*src && *src != '&' && *src != ' ' && n + 1 < dstSize)
    {
        if (*src == '%' && src[1] && src[2]) { char hex[3] = { src[1], src[2], 0 }; dst[n++] = (char)strtol(hex, nullptr, 16); src += 3; }
        else dst[n++] = *src++ == '+' ? ' ' : src[-1];
    }
    dst[n] = 0;
}

/* This listener deliberately lives off the game thread.  QuickJS can block
 * that thread at a source breakpoint while this endpoint continues receiving
 * Continue/Step/Inspect commands from the standalone DevTools window. */
static void *vbjs_web_debugger_thread(void *)
{
    for (;;)
    {
        int fd = (int)accept(s_webDebuggerListen, nullptr, nullptr);
        if (fd < 0) break;
        char request[16384]; int bytes = (int)recv(fd, request, sizeof(request) - 1, 0); request[bytes > 0 ? bytes : 0] = 0;
        if (strncmp(request, "GET /send?m=", 12) == 0)
        {
            char json[8192]; vbjs_web_decode(json, sizeof(json), request + 12);
            VBJS_DevtoolsDebuggerSend(json); vbjs_web_reply(fd, "queued");
        }
        else if (strncmp(request, "GET /terminate", 14) == 0)
        {
            // Duke treats SDL_QUIT as an Escape/menu action while in-game, so
            // that does not reliably close the process. A debugger Stop must
            // terminate the game even for an attached session (no Node child
            // PID is available in that case). Reply first, then use SIGTERM;
            // the engine's installed signal handler flushes and exits cleanly.
            vbjs_web_reply(fd, "terminating");
            vbjs_web_close(fd);
#ifdef _WIN32
            TerminateProcess(GetCurrentProcess(), 0);
#else
            kill(getpid(), SIGTERM);
#endif
            continue;
        }
        else if (strncmp(request, "GET /poll", 9) == 0)
        {
            int const vscode = strstr(request, "client=vscode") != nullptr;
            char json[8192]; if (!VBJS_DevtoolsDebuggerReceiveForClient(vscode, json, sizeof(json))) strcpy(json, "");
            vbjs_web_reply(fd, json, "application/json; charset=utf-8");
        }
        else vbjs_web_reply(fd, "not found");
        vbjs_web_close(fd);
    }
    return nullptr;
}

static void vbjs_web_debugger_start(void)
{
    if (s_webDebuggerThreadStarted) return;
#ifdef _WIN32
    WSADATA wsa; if (WSAStartup(MAKEWORD(2, 2), &wsa)) return;
#endif
    s_webDebuggerListen = (int)socket(AF_INET, SOCK_STREAM, 0); if (s_webDebuggerListen < 0) return;
    int one = 1; setsockopt(s_webDebuggerListen, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof(one));
    // A stable, localhost-only port lets an external Debug Adapter attach to
    // a running game. The browser inspector uses the same bridge internally.
    sockaddr_in address {}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); address.sin_port = htons(VBJS_DebugPort());
    if (bind(s_webDebuggerListen, (sockaddr *)&address, sizeof(address)) || listen(s_webDebuggerListen, 8)) { vbjs_web_close(s_webDebuggerListen); s_webDebuggerListen = -1; return; }
    socklen_t length = sizeof(address); getsockname(s_webDebuggerListen, (sockaddr *)&address, &length); s_webDebuggerPort = ntohs(address.sin_port);
    if (!pthread_create(&s_webDebuggerThread, nullptr, vbjs_web_debugger_thread, nullptr)) s_webDebuggerThreadStarted = 1;
    else { vbjs_web_close(s_webDebuggerListen); s_webDebuggerListen = -1; }
}

#ifdef __APPLE__
// ---- Browser DevTools window (macOS): served over its own localhost listener.
static int s_webDevtoolsListen = -1;
static int s_webDevtoolsPort = 0;
static int s_webDevtoolsLaunched = 0;

static void vbjs_web_devtools_focus(void)
{
    if (s_webDevtoolsPort <= 0) return;
    char url[96]; snprintf(url, sizeof(url), "http://127.0.0.1:%d/", s_webDevtoolsPort);
    if (fork() == 0) { execl("/usr/bin/open", "open", "-a", "Google Chrome", url, (char *)nullptr); _exit(127); }
}

static void vbjs_web_devtools_tick(void)
{
    // SDL reapplies its saved window coordinates during early video setup on
    // Cocoa.  Keep the debug-layout game position authoritative while -dt is
    // active instead of allowing a one-frame snap back to the right.
    VBJS_CocoaPlaceGameLeft(SDL_GL_GetCurrentWindow());
    if (s_webDevtoolsListen < 0)
    {
        s_webDevtoolsListen = socket(AF_INET, SOCK_STREAM, 0);
        if (s_webDevtoolsListen < 0) return;
        int one = 1; setsockopt(s_webDevtoolsListen, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in address {}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); address.sin_port = 0;
        if (bind(s_webDevtoolsListen, (sockaddr *)&address, sizeof(address)) || listen(s_webDevtoolsListen, 4)) { close(s_webDevtoolsListen); s_webDevtoolsListen = -1; return; }
        socklen_t length = sizeof(address); getsockname(s_webDevtoolsListen, (sockaddr *)&address, &length); s_webDevtoolsPort = ntohs(address.sin_port);
        fcntl(s_webDevtoolsListen, F_SETFL, fcntl(s_webDevtoolsListen, F_GETFL) | O_NONBLOCK);
    }
    vbjs_web_debugger_start();
    if (!s_webDevtoolsLaunched)
    {
        s_webDevtoolsLaunched = 1;
        char url[128]; snprintf(url, sizeof(url), "--app=http://127.0.0.1:%d/", s_webDevtoolsPort);
        if (fork() == 0) { execl("/usr/bin/open", "open", "-na", "Google Chrome", "--args", url, "--window-size=516,666", (char *)nullptr); _exit(127); }
    }
    for (;;)
    {
        int fd = accept(s_webDevtoolsListen, nullptr, nullptr);
        if (fd < 0) break;
        char request[8192]; int bytes = (int)recv(fd, request, sizeof(request)-1, 0); request[bytes > 0 ? bytes : 0] = 0;
        if (strncmp(request, "GET /api/log", 12) == 0)
        {
            char body[8192] = "";
            for (int i = 0; i < 12; ++i) { char const *line = VBJS_DevtoolsLog(i); if (line) { strncat(body, line, sizeof(body)-strlen(body)-2); strncat(body, "\n", sizeof(body)-strlen(body)-1); } }
            vbjs_web_reply(fd, body);
        }
        else if (strncmp(request, "GET /api/eval?e=", 16) == 0)
        {
            char expression[1024], result[2048]; vbjs_web_decode(expression, sizeof(expression), request + 16);
            VBJS_DevtoolsEvaluate(expression, result, sizeof(result)); vbjs_web_reply(fd, result);
        }
        else if (strncmp(request, "GET /api/sources", 16) == 0)
        {
            char body[4096] = "";
            for (int i = 0; i < VBJS_DevtoolsSourceCount(); ++i) { char const *path = VBJS_DevtoolsSourcePath(i); if (path) { strncat(body, path, sizeof(body)-strlen(body)-2); strncat(body, "\n", sizeof(body)-strlen(body)-1); } }
            vbjs_web_reply(fd, body);
        }
        else if (strncmp(request, "GET /api/source?i=", 18) == 0)
        {
            int index = atoi(request + 18); char body[32768];
            if (!VBJS_DevtoolsReadSource(index, body, sizeof(body))) snprintf(body, sizeof(body), "Unable to read source.");
            vbjs_web_reply(fd, body);
        }
        else
        {
            static char const page[] = R"HTML(<!doctype html><meta charset=utf-8><title>NNBuild JavaScript DevTools</title><style>body{margin:0;background:#1e1e1e;color:#ddd;font:14px ui-monospace,Menlo,monospace}header{padding:10px;border-bottom:1px solid #555;font:600 16px system-ui}main{display:grid;grid-template-columns:220px 1fr;height:calc(100vh - 42px)}aside{padding:8px;border-right:1px solid #555;overflow:auto}button{margin:3px;padding:6px;background:#367bd6;color:white;border:0;border-radius:4px}pre{margin:0;padding:10px;overflow:auto;white-space:pre-wrap}#source{height:52vh;border-bottom:1px solid #555}#log{height:20vh}input{width:72%;background:#282828;color:#eee;border:1px solid #666;padding:7px;font:inherit}</style><header>NNBuild JavaScript DevTools <button onclick=refreshSources()>Sources</button></header><main><aside><b>Sources</b><div id=files></div></aside><section><pre id=source>Select a script.</pre><pre id=log>Loading…</pre><input id=e value="$event" title="Inspect globals or the paused event"><button onclick=run()>Evaluate / Hover inspection</button></section></main><script>const log=document.querySelector('#log'),src=document.querySelector('#source');async function refresh(){log.textContent=await (await fetch('/api/log')).text();setTimeout(refresh,300)}async function run(){let r=await (await fetch('/api/eval?e='+encodeURIComponent(e.value))).text();log.textContent='> '+e.value+'\n'+r}async function refreshSources(){let a=(await (await fetch('/api/sources')).text()).trim().split('\n');files.innerHTML='';a.forEach((n,i)=>{let b=document.createElement('button');b.textContent=n.split('/').pop();b.onclick=async()=>src.textContent=await (await fetch('/api/source?i='+i)).text();files.append(b,document.createElement('br'))})}e.onkeydown=x=>{if(x.key==='Enter')run()};refresh();refreshSources()</script>)HTML";
            char body[16384];
            static char const debuggerClient[] = R"HTML(
<script>
window.vbDebugPort=%d;
(()=>{
 let seq=1,currentPath='',sourceText='',currentFrame=0,paused=false,pausedPath='',pausedLine=0,breakpoints={},pending=new Set();
 const endpoint='http://127.0.0.1:'+window.vbDebugPort;
 const section=document.querySelector('section'), toolbar=document.createElement('div');
 toolbar.style.cssText='padding:6px;border-bottom:1px solid #555';
 const stack=document.createElement('pre'), scopes=document.createElement('pre');
 stack.style.cssText=scopes.style.cssText='margin:0;padding:8px;max-height:16vh;overflow:auto;border-top:1px solid #555;color:#9cdcfe';
 section.append(toolbar,stack,scopes);
 function button(label,command){let b=document.createElement('button');b.textContent=label;b.onclick=()=>request(command,{});toolbar.append(b)}
 button('Continue','continue');button('Step over','next');button('Step into','stepIn');button('Step out','stepOut');
 async function send(m){await fetch(endpoint+'/send?m='+encodeURIComponent(JSON.stringify(m)))}
 function request(command,args){let request_seq=seq++;pending.add(request_seq);return send({type:'request',request:{request_seq,command,args:args||{}}})}
 window.cont=()=>request('continue'); window.run=()=>request('evaluate',{expression:e.value,frame:currentFrame});
 const style=document.createElement('style');style.textContent='#source{white-space:pre!important;line-height:16px}.vb-line{display:block}.vb-line.paused{background:#5b4720}.vb-gutter{display:inline-block;width:2.4em;color:#777;cursor:pointer;text-align:center}.vb-gutter.set{color:#ff5f56;font-weight:bold}.vb-number{display:inline-block;width:3em;color:#777;text-align:right;padding-right:1em;user-select:none}.tok-key{color:#c586c0}.tok-str{color:#ce9178}.tok-num{color:#b5cea8}.tok-com{color:#6a9955}.tok-type{color:#4ec9b0}';document.head.append(style);
 const escapeHtml=s=>s.replace(/[&<>]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]));
 const highlight=s=>{let out='',last=0,re=/\/\/.*$|\/\*[\s\S]*?\*\/|`(?:\\.|[^`])*`|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|\b(?:const|let|var|function|return|if|else|for|while|switch|case|break|continue|new|class|extends|import|export|from|async|await|throw|try|catch|finally|interface|type|enum|as|readonly|public|private|protected|declare|implements)\b|\b(?:string|number|boolean|unknown|any|void|never|object|undefined|null)\b|\b\d+(?:\.\d+)?\b/g;s.replace(re,m=>{out+=escapeHtml(s.slice(last,m.index));let t=escapeHtml(m[0]),k=m[0][0]==='/'?'com':m[0][0]==='"'||m[0][0]==="'"||m[0][0]==='`'?'str':/^\d/.test(m[0])?'num':/^(string|number|boolean|unknown|any|void|never|object|undefined|null)$/.test(m[0])?'type':'key';out+=`<span class="tok-${k}">${t}</span>`;last=m.index+m[0].length;return m[0]});return out+escapeHtml(s.slice(last));};
 function renderSource(){src.innerHTML=sourceText.split('\n').map((text,i)=>{let line=i+1,key=currentPath+':'+line,set=!!breakpoints[key],at=pausedPath===currentPath&&pausedLine===line;return `<span class="vb-line ${at?'paused':''}" data-line="${line}"><span class="vb-gutter ${set?'set':''}" title="Toggle breakpoint">${set?'●':'○'}</span><span class="vb-number">${line}</span>${highlight(text)}</span>`}).join('');let at=src.querySelector('.vb-line.paused');if(at)at.scrollIntoView({block:'center'});}
 window.refreshSources=async()=>{let a=(await (await fetch('/api/sources')).text()).trim().split('\n');files.innerHTML='';a.forEach((path,i)=>{let b=document.createElement('button');b.textContent=path.split('/').pop();b.onclick=async()=>{currentPath=path;sourceText=await (await fetch('/api/source?i='+i)).text();renderSource();};files.append(b,document.createElement('br'))})};
 src.title='Click the circle in the left gutter to toggle a breakpoint';
 src.onclick=ev=>{if(!ev.target.closest('.vb-gutter'))return;let row=ev.target.closest('.vb-line');if(!currentPath||!row)return;let line=+row.dataset.line,key=currentPath+':'+line;breakpoints[key]=!breakpoints[key];let lines=Object.keys(breakpoints).filter(k=>breakpoints[k]&&k.startsWith(currentPath+':')).map(k=>({line:+k.slice(currentPath.length+1)}));send({type:'breakpoints',breakpoints:{path:currentPath,breakpoints:lines}});renderSource();stack.textContent=(breakpoints[key]?'Breakpoint set: ':'Breakpoint cleared: ')+key;};
 function renderScopes(items){scopes.textContent=items.map(x=>x.name||'Scope').join('\n');if(items[0])request('variables',{variablesReference:items[0].reference})}
 async function poll(){try{let text=await (await fetch(endpoint+'/poll')).text();if(text){let m=JSON.parse(text),body=m.body;if(m.type==='event'){paused=true;pausedPath=m.event.filename||'';pausedLine=m.event.line||0;if(pausedPath===currentPath)renderSource();stack.textContent='Paused — '+m.event.reason+'\n'+pausedPath+':'+pausedLine;request('stackTrace')}else if(m.type==='response'&&pending.delete(m.request_seq)){if(Array.isArray(body)){if(body.length&&body[0].filename!==undefined){stack.textContent=body.map((f,i)=>`${i===currentFrame?'▶ ':'  '}${f.name}  ${f.filename||''}:${f.line||''}`).join('\n');stack.onclick=ev=>{currentFrame=Math.max(0,Math.floor(ev.offsetY/16));request('scopes',{frame:currentFrame})};request('scopes',{frame:currentFrame})}else renderScopes(body)}else if(body&&body.result!==undefined)log.textContent='> '+e.value+'\n'+body.result;else if(body&&typeof body==='object')scopes.textContent=Object.entries(body).map(([k,v])=>k+' = '+String(v)).join('\n');}}}catch(_){}setTimeout(poll,70)}
 poll();refreshSources();
})();
</script>)HTML";
            snprintf(body, sizeof(body), "%s", page);
            size_t pageLength = strlen(body);
            snprintf(body + pageLength, sizeof(body) - pageLength, debuggerClient, s_webDebuggerPort);
            vbjs_web_reply(fd, body, "text/html; charset=utf-8");
        }
        close(fd);
    }
}
#endif

static void vbjs_devtools_draw_contents(void)
{
    static char expression[256] = "$event";
    static char result[1024] = "";

    int windowW = 516, windowH = 666;
    if (s_devtoolsWindow) SDL_GetWindowSize(s_devtoolsWindow, &windowW, &windowH);
    ImGui::SetNextWindowPos(ImVec2(8.f, 8.f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2((float)windowW - 16.f, (float)windowH - 16.f), ImGuiCond_Always);
    ImGuiWindowFlags const flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
    ImGui::Begin("NNBuild JavaScript DevTools", nullptr, flags);

    if (VBJS_DevtoolsPaused())
    {
        ImGui::TextColored(ImVec4(1.f, .75f, .1f, 1.f), "PAUSED at %s", VBJS_DevtoolsCurrentEvent() ? VBJS_DevtoolsCurrentEvent() : "breakpoint");
        ImGui::SameLine();
        if (ImGui::Button("Resume")) VBJS_DevtoolsResume();
    }
    else ImGui::TextDisabled("Running");

    if (ImGui::BeginTabBar("js-devtools-tabs"))
    {
        if (ImGui::BeginTabItem("Console"))
        {
            ImGui::BeginChild("console-log", ImVec2(0, -36), ImGuiChildFlags_Borders);
            for (int i = 0; i < 12; ++i)
            {
                const char *line = VBJS_DevtoolsLog(i);
                if (line) ImGui::TextWrapped("%s", line);
            }
            ImGui::EndChild();
            if (ImGui::InputText("Evaluate", expression, sizeof(expression), ImGuiInputTextFlags_EnterReturnsTrue))
                VBJS_DevtoolsEvaluate(expression, result, sizeof(result));
            if (ImGui::Button("Run")) VBJS_DevtoolsEvaluate(expression, result, sizeof(result));
            ImGui::SameLine(); ImGui::TextWrapped("%s", result);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Scope"))
        {
            ImGui::TextUnformatted("Evaluate global expressions or inspect the last event.");
            ImGui::BulletText("$event: %s", VBJS_DevtoolsCurrentEvent() ? VBJS_DevtoolsCurrentEvent() : "none");
            ImGui::BulletText("Examples: vb.player(), globalThis.myValue");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

static void vbjs_devtools_open_window(void)
{
    if (s_devtoolsWindow || s_devtoolsWindowClosed) return;
    SDL_Window *const gameWindow = SDL_GL_GetCurrentWindow();
    SDL_GLContext const gameContext = SDL_GL_GetCurrentContext();
    int gameX = SDL_WINDOWPOS_CENTERED, gameY = SDL_WINDOWPOS_CENTERED, gameW = 1024, gameH = 768;
    if (gameWindow) { SDL_GetWindowPosition(gameWindow, &gameX, &gameY); SDL_GetWindowSize(gameWindow, &gameW, &gameH); }
    SDL_Rect usable = { 0, 0, 1920, 1080 };
    int const display = gameWindow ? SDL_GetWindowDisplayIndex(gameWindow) : 0;
    SDL_GetDisplayUsableBounds(display >= 0 ? display : 0, &usable);
    int const gap = 12;
    int const devW = max(360, min(516, usable.w - gameW - gap));
    int const devH = min(666, usable.h - 24);
    if (gameW + gap + devW <= usable.w)
    {
        gameX = usable.x + (usable.w - gameW - gap - devW) / 2;
        gameY = usable.y + max(12, (usable.h - gameH) / 2);
        if (gameWindow) SDL_SetWindowPosition(gameWindow, gameX, gameY);
    }
    else
    {
        gameX = max(usable.x, min(gameX, usable.x + usable.w - gameW));
        gameY = max(usable.y, min(gameY, usable.y + usable.h - gameH));
    }
    int const devX = min(usable.x + usable.w - devW, gameX + gameW + gap);
    int const devY = max(usable.y, min(gameY, usable.y + usable.h - devH));
    // SDL relative mouse mode is process-wide on macOS. Release it before the
    // second window exists, otherwise its first mouse click is captured by the
    // game and Cocoa immediately reactivates the game window.
    if (gameWindow) SDL_SetWindowGrab(gameWindow, SDL_FALSE);
    SDL_SetRelativeMouseMode(SDL_FALSE);
    // SDL's default drops the mouse-down that gives a secondary window focus.
    // Set this before creating DevTools so that first click reaches ImGui.
#ifdef SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
#endif
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
    s_devtoolsWindow = SDL_CreateWindow("NNBuild JavaScript DevTools", devX, devY, devW, devH,
                                         SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!s_devtoolsWindow) { LOG_F(ERROR, "Could not open JavaScript DevTools window: %s", SDL_GetError()); return; }
    s_devtoolsContext = SDL_GL_CreateContext(s_devtoolsWindow);
    if (!s_devtoolsContext)
    {
        LOG_F(ERROR, "Could not create JavaScript DevTools context: %s", SDL_GetError());
        SDL_DestroyWindow(s_devtoolsWindow); s_devtoolsWindow = nullptr;
        return;
    }
    s_devtoolsWindowId = SDL_GetWindowID(s_devtoolsWindow);
    ImGuiContext *const old = ImGui::GetCurrentContext();
    s_devtoolsImGui = ImGui::CreateContext();
    ImGui::SetCurrentContext(s_devtoolsImGui);
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NoMouseCursorChange;
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplSDL2_InitForOpenGL(s_devtoolsWindow, s_devtoolsContext);
    // NNBuild's macOS renderer can expose an OpenGL 2.1 context; the
    // backend default (#version 150) fails there, while GLSL 120 is portable.
    ImGui_ImplOpenGL3_Init("#version 120");
    ImGui::GetIO().Fonts->AddFontDefault();
    ImGui::SetCurrentContext(old);
    SDL_GL_MakeCurrent(gameWindow, gameContext);
    // Explicitly make this a normal foreground Cocoa window.  Without this,
    // a relative-mode game window can retain the first mouse activation and
    // immediately pull keyboard/mouse focus back to itself.
    SDL_RaiseWindow(s_devtoolsWindow);
#ifdef __APPLE__
    VBJS_CocoaMakeKeyWindow(s_devtoolsWindow);
#endif
}

static void vbjs_devtools_render_window(void)
{
    vbjs_devtools_open_window();
    if (!s_devtoolsWindow || !s_devtoolsContext || !s_devtoolsImGui) return;
    SDL_Window *const gameWindow = SDL_GL_GetCurrentWindow();
    SDL_GLContext const gameContext = SDL_GL_GetCurrentContext();
    ImGuiContext *const old = ImGui::GetCurrentContext();
    SDL_GL_MakeCurrent(s_devtoolsWindow, s_devtoolsContext);
    ImGui::SetCurrentContext(s_devtoolsImGui);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    vbjs_devtools_draw_contents();
    ImGui::Render();
    int w, h; SDL_GL_GetDrawableSize(s_devtoolsWindow, &w, &h);
    glViewport(0, 0, w, h);
    glClearColor(.08f, .08f, .08f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(s_devtoolsWindow);
    ImGui::SetCurrentContext(old);
    SDL_GL_MakeCurrent(gameWindow, gameContext);
}

extern "C" int VBJS_DevtoolsProcessEvent(const void *event)
{
#ifdef __APPLE__
    if (VBJS_CocoaDevtoolsProcessEvent(event)) return 1;
#endif
    if (!s_devtoolsWindow || !s_devtoolsImGui || !event) return 0;
    SDL_Event const *const ev = (SDL_Event const *)event;
    Uint32 id = 0;
    switch (ev->type)
    {
        case SDL_WINDOWEVENT: id = ev->window.windowID; break;
        case SDL_KEYDOWN: case SDL_KEYUP: id = ev->key.windowID; break;
        case SDL_TEXTINPUT: id = ev->text.windowID; break;
        case SDL_MOUSEMOTION: id = ev->motion.windowID; break;
        case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP: id = ev->button.windowID; break;
        case SDL_MOUSEWHEEL: id = ev->wheel.windowID; break;
        default: return 0;
    }
    bool const mouseEvent = ev->type == SDL_MOUSEMOTION || ev->type == SDL_MOUSEBUTTONDOWN ||
                            ev->type == SDL_MOUSEBUTTONUP || ev->type == SDL_MOUSEWHEEL;
    bool const keyboardEvent = ev->type == SDL_KEYDOWN || ev->type == SDL_KEYUP || ev->type == SDL_TEXTINPUT;
    // Cocoa can give an input event windowID 0 during the handoff out of
    // SDL relative mouse mode.  The actual SDL focus is authoritative here.
    if (id != s_devtoolsWindowId &&
        !(mouseEvent && SDL_GetMouseFocus() == s_devtoolsWindow) &&
        !(keyboardEvent && SDL_GetKeyboardFocus() == s_devtoolsWindow)) return 0;
    if (ev->type == SDL_WINDOWEVENT && ev->window.event == SDL_WINDOWEVENT_CLOSE)
    {
        SDL_HideWindow(s_devtoolsWindow);
        s_devtoolsWindowClosed = 1;
        return 1;
    }
    ImGuiContext *const old = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(s_devtoolsImGui);
    ImGui_ImplSDL2_ProcessEvent(ev);
    ImGui::SetCurrentContext(old);
    return 1;
}

extern "C" int VBJS_DevtoolsOwnsInput(void)
{
    return s_devtoolsWindow &&
           (SDL_GetKeyboardFocus() == s_devtoolsWindow || SDL_GetMouseFocus() == s_devtoolsWindow);
}

extern "C" int VBJS_DevtoolsSuppressGameMouseCapture(void)
{
#ifdef __APPLE__
    return VBJS_DevtoolsEnabled();
#else
    return s_devtoolsWindow != nullptr && !s_devtoolsWindowClosed;
#endif
}

extern "C" void VBJS_DevtoolsFocus(void)
{
#ifdef __APPLE__
    vbjs_web_devtools_focus();
#endif
}

extern "C" void VBJS_DevtoolsStartDebuggerBridge(void)
{
    vbjs_web_debugger_start();
}

extern "C" void VBJS_DevtoolsShutdownWindow(void)
{
#ifdef __APPLE__
    if (s_webDevtoolsListen >= 0) { close(s_webDevtoolsListen); s_webDevtoolsListen = -1; }
#endif
    if (s_webDebuggerListen >= 0)
    {
        // Wake accept() before joining so no debugger thread survives engine
        // teardown.  Closing alone is not sufficient on every socket
        // implementation when it happens from a different thread.
#ifdef _WIN32
        shutdown((SOCKET)s_webDebuggerListen, SD_BOTH);
#else
        shutdown(s_webDebuggerListen, SHUT_RDWR);
#endif
        vbjs_web_close(s_webDebuggerListen); s_webDebuggerListen = -1;
    }
    if (s_webDebuggerThreadStarted)
    {
        pthread_join(s_webDebuggerThread, nullptr);
        s_webDebuggerThreadStarted = 0;
    }
    if (!s_devtoolsWindow) return;
    ImGuiContext *const old = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(s_devtoolsImGui);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext(s_devtoolsImGui);
    ImGui::SetCurrentContext(old);
    SDL_GL_DeleteContext(s_devtoolsContext);
    SDL_DestroyWindow(s_devtoolsWindow);
    s_devtoolsWindow = nullptr; s_devtoolsContext = nullptr; s_devtoolsImGui = nullptr;
    s_devtoolsWindowId = 0;
}
#else
extern "C" int VBJS_DevtoolsProcessEvent(const void *) { return 0; }
extern "C" int VBJS_DevtoolsOwnsInput(void) { return 0; }
extern "C" int VBJS_DevtoolsSuppressGameMouseCapture(void) { return 0; }
extern "C" void VBJS_DevtoolsFocus(void) {}
extern "C" void VBJS_DevtoolsStartDebuggerBridge(void) {}
extern "C" void VBJS_DevtoolsShutdownWindow(void) {}
#endif

// HUD draw: render every script-registered text line (called from the overlay pass).
extern "C" void VBJS_DrawHUD(void)
{
    int n = 0;
    vbjs_text_t *t = VBJS_TextSlots(&n);
    for (int i = 0; i < n; i++)
        if (t[i].used)
            G_PrintGameText(0, STARTALPHANUM, t[i].x, t[i].y, t[i].str, t[i].shade, t[i].pal,
                            8 + 16, 0, 0, xdim - 1, ydim - 1, 65536);

#if SDL_MAJOR_VERSION >= 2 && defined(USE_OPENGL)
    if (VBJS_DevtoolsEnabled())
# ifdef __APPLE__
        vbjs_web_devtools_tick();
# else
        vbjs_devtools_render_window();
# endif
#else
    // Software-renderer fallback: retain a small read-only console panel.
    if (VBJS_DevtoolsEnabled())
    {
        G_PrintGameText(0, STARTALPHANUM, 218, 4, "JS DEVTOOLS", 0, 14, 8 + 16, 0, 0, xdim - 1, ydim - 1, 65536);
        G_PrintGameText(0, STARTALPHANUM, 218, 12, "Console (F12 opens OSD)", 0, 8, 8 + 16, 0, 0, xdim - 1, ydim - 1, 65536);
        for (int i = 0; i < 12; ++i)
        {
            const char *line = VBJS_DevtoolsLog(i);
            if (line) G_PrintGameText(0, STARTALPHANUM, 218, 22 + i * 8, line, 0, 7, 8 + 16, 0, 0, xdim - 1, ydim - 1, 65536);
        }
    }
#endif
}
