#include "editormcp.h"

#include "duke3d.h"
#include "editorcontrol.h"
#include "editorprefab.h"
#include "enet.h"
#include "oldnet.h"
#include "serverlevel.h"
#include "sjson.h"

#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

#ifdef _WIN32
# include <io.h>
# include "windows_inc.h"
#else
# include <fcntl.h>
# include <signal.h>
# include <unistd.h>
#endif

#if defined(RENDERTYPESDL) && SDL_TARGET >= 2
# include "sdl_inc.h"
#endif

namespace {

using namespace DukeEditor;
using Clock = std::chrono::steady_clock;

constexpr size_t maxRequestBytes = 128 * 1024;
constexpr size_t maxHeaders = 8192;
constexpr char const *objectNames[] = {"none", "sector", "wall", "vertex", "sprite", "start"};
constexpr char const *activityNames[] = {"select", "draw_room", "place_sprite", "place_start", "drag", "playtest"};

char const tools[] = R"json({
 "tools":[
  {"name":"editor_status","description":"Read the session, participant roster and current draft revision.",
   "inputSchema":{"type":"object","properties":{},"additionalProperties":false}},
  {"name":"editor_connect","description":"Join or reconnect to a VibeBuild32 live editor as an AI participant.",
   "inputSchema":{"type":"object","properties":{"address":{"type":"string"},"name":{"type":"string"}},"additionalProperties":false}},
  {"name":"editor_disconnect","description":"Leave the live editor while keeping this MCP adapter open.",
   "inputSchema":{"type":"object","properties":{},"additionalProperties":false}},
  {"name":"editor_inspect","description":"Read a map summary or a bounded range of classic records. Indexes start at zero.",
   "inputSchema":{"type":"object","properties":{"kind":{"enum":["map","sectors","walls","sprites"]},"index":{"type":"integer","minimum":0},"count":{"type":"integer","minimum":1,"maximum":80}},"additionalProperties":false}},
  {"name":"editor_presence","description":"Move your visible cursor and select an object for other participants to see.",
   "inputSchema":{"type":"object","properties":{"kind":{"enum":["none","sector","wall","vertex","sprite","start"]},"index":{"type":"integer","minimum":0},"x":{"type":"integer"},"y":{"type":"integer"},"clear":{"type":"boolean"}},"additionalProperties":false}},
  {"name":"editor_edit","description":"Commit one edit against a revision you inspected. Map coordinates are Build units. Use editor_inspect for record indexes and properties.",
   "inputSchema":{"type":"object","required":["revision","operation"],"properties":{
      "revision":{"type":"integer","minimum":1},
      "operation":{"enum":["room","extend","split","move_vertex","sprite","move_sprite","start","set","delete","batch"]},
      "label":{"type":"string","maxLength":64},
      "edits":{"type":"array","minItems":1,"maxItems":64,"items":{"type":"object","required":["operation"],"properties":{
        "operation":{"enum":["room","extend","split","move_vertex","sprite","move_sprite","start","set","delete"]},
        "kind":{"enum":["sector","wall","sprite","start"]},"index":{"type":"integer","minimum":0},"sector":{"type":"integer","minimum":0},
        "x":{"type":"integer"},"y":{"type":"integer"},"angle":{"type":"integer","minimum":0,"maximum":2047},
        "tile":{"type":"integer","minimum":0},"depth":{"type":"integer","minimum":128,"maximum":32768},
        "points":{"type":"array","minItems":3,"maxItems":8192,"items":{"type":"array","minItems":2,"maxItems":2,"items":{"type":"integer"}}},
        "properties":{"type":"object","additionalProperties":{"type":"integer"}}
      },"additionalProperties":false}},
      "kind":{"enum":["sector","wall","sprite","start"]},
      "index":{"type":"integer","minimum":0},"sector":{"type":"integer","minimum":0},
      "x":{"type":"integer"},"y":{"type":"integer"},"angle":{"type":"integer","minimum":0,"maximum":2047},
      "tile":{"type":"integer","minimum":0},"depth":{"type":"integer","minimum":128,"maximum":32768},
      "points":{"type":"array","minItems":3,"maxItems":8192,"items":{"type":"array","minItems":2,"maxItems":2,"items":{"type":"integer"}}},
      "properties":{"type":"object","additionalProperties":{"type":"integer"}}
   },"additionalProperties":false}},
  {"name":"editor_history","description":"Read recent grouped changes and the participant and objects associated with each revision.",
   "inputSchema":{"type":"object","properties":{"count":{"type":"integer","minimum":1,"maximum":24}},"additionalProperties":false}},
  {"name":"editor_export","description":"Save a classic version 7 .MAP on the machine running this MCP adapter.",
   "inputSchema":{"type":"object","required":["path"],"properties":{"path":{"type":"string"},"overwrite":{"type":"boolean","default":false}},"additionalProperties":false}}
 ]
})json";

char const workspaceTools[] = R"json([
 {"name":"editor_check","description":"Check the local map for missing assets, orphaned switches, placement issues, and game compatibility.",
  "inputSchema":{"type":"object","properties":{"index":{"type":"integer","minimum":0},"count":{"type":"integer","minimum":1,"maximum":80}},"additionalProperties":false}},
 {"name":"editor_control","description":"Control the editor workspace in this game instance. Save or discard an unsaved draft explicitly before replacing it.",
  "inputSchema":{"type":"object","required":["action"],"properties":{
    "action":{"enum":["enter","new","open","save","undo","redo","playtest","stop_playtest","close","plan","perspective","frame","host","join","leave","build_tools","section"]},
    "path":{"type":"string"},"name":{"type":"string"},"address":{"type":"string"},
    "spawn":{"type":"integer","minimum":0},"x":{"type":"integer"},"y":{"type":"integer"},
    "sector":{"type":"integer","minimum":0},"angle":{"type":"integer","minimum":0,"maximum":2047},
    "platform":{"type":"integer","minimum":0},
    "target":{"enum":["inherit","duke13","duke_atomic","nam","napalm","ww2gi","standalone"]},
    "port":{"type":"integer","minimum":1,"maximum":65535},"overwrite":{"type":"boolean"},"discard":{"type":"boolean"}
  },"additionalProperties":false}},
 {"name":"project_status","description":"Read the project's base game, entry map, modern options and asset browser. Assets include their effective game and icon.",
  "inputSchema":{"type":"object","properties":{"index":{"type":"integer","minimum":0},"count":{"type":"integer","minimum":1,"maximum":80}},"additionalProperties":false}},
 {"name":"project_control","description":"Manage a project, create or attach JS/TS components, or save and place sprite prefabs.",
  "inputSchema":{"type":"object","required":["action"],"properties":{
    "action":{"enum":["create","open","close","import_zip","export_zip","add_asset","settings","map_settings","attach","detach","export_map",
                      "create_component","save_prefab","place_prefab"]},
    "path":{"type":"string"},"destination":{"type":"string"},"name":{"type":"string"},"entry":{"type":"string"},
    "target":{"enum":["inherit","duke13","duke_atomic","nam","napalm","ww2gi","standalone"]},
    "modern":{"enum":["inherit","off","on"]},"kind":{"enum":["sector","wall","sprite"]},"script":{"type":"string"},
    "sprites":{"type":"array","minItems":1,"maxItems":4096,"items":{"type":"integer","minimum":0}},
    "x":{"type":"integer"},"y":{"type":"integer"},"sector":{"type":"integer","minimum":0},
    "floor_z":{"type":"integer"},
    "index":{"type":"integer","minimum":-1},"overwrite":{"type":"boolean"}
  },"additionalProperties":false}}
])json";

char const gameTools[] = R"json([
 {"name":"game_status","description":"Read the current application mode, active map and local player state.",
  "inputSchema":{"type":"object","properties":{},"additionalProperties":false}},
 {"name":"server_status","description":"Read the local multiplayer role, settings and connected player roster.",
  "inputSchema":{"type":"object","properties":{},"additionalProperties":false}},
 {"name":"console_execute","description":"Execute a line in this instance's game console. Use listsymbols or help NAME to discover commands. Game server commands use the same console.",
  "inputSchema":{"type":"object","required":["command"],"properties":{"command":{"type":"string","minLength":1,"maxLength":4096}},"additionalProperties":false}},
 {"name":"console_output","description":"Read recent game console messages, optionally after a previously returned cursor.",
  "inputSchema":{"type":"object","properties":{"after":{"type":"integer","minimum":0},"limit":{"type":"integer","minimum":1,"maximum":80}},"additionalProperties":false}},
 {"name":"ui_input","description":"Send keyboard, mouse or text input through the game's SDL event queue. Send releases in a later call so the game can process held inputs.",
  "inputSchema":{"type":"object","required":["events"],"properties":{"events":{"type":"array","minItems":1,"maxItems":32,
    "items":{"type":"object","required":["type"],"properties":{
    "type":{"enum":["key_down","key_up","move","button_down","button_up","wheel","text"]},
    "key":{"type":"string"},"button":{"enum":["left","middle","right"]},"text":{"type":"string","maxLength":31},
    "x":{"type":"integer"},"y":{"type":"integer"},"dx":{"type":"integer"},"dy":{"type":"integer"}
  },"additionalProperties":false}}},"additionalProperties":false}}
])json";

char const *stringOf(sjson_node *node, char const *name)
{
    auto const value = sjson_find_member(node, name);
    return value && value->tag == SJSON_STRING ? value->string_ : "";
}

bool boolOf(sjson_node *node, char const *name)
{
    auto const value = sjson_find_member(node, name);
    return value && value->tag == SJSON_BOOL && value->bool_;
}

bool integer(sjson_node *value, int64_t low, int64_t high, int64_t &out)
{
    if (!value || value->tag != SJSON_NUMBER || !std::isfinite(value->number_) ||
        value->number_ < double(low) || value->number_ > double(high) ||
        std::trunc(value->number_) != value->number_)
        return false;
    out = static_cast<int64_t>(value->number_);
    return true;
}

bool argument(sjson_node *input, char const *key, int low, int high, int &out)
{
    int64_t value;
    if (!integer(sjson_find_member(input, key), low, high, value))
        return false;
    out = int(value);
    return true;
}

bool pointArgument(sjson_node *input, vec2_t &point)
{
    return argument(input, "x", -BXY_MAX, BXY_MAX, point.x) && argument(input, "y", -BXY_MAX, BXY_MAX, point.y);
}

void putPoint(sjson_context *ctx, sjson_node *out, vec2_t point)
{
    sjson_put_int(ctx, out, "x", point.x);
    sjson_put_int(ctx, out, "y", point.y);
}

struct ConsoleLine
{
    uint64_t id;
    std::string text;
};

std::mutex consoleMutex;
std::deque<ConsoleLine> consoleLines;
uint64_t consoleCursor;

void captureConsole(void *, loguru::Message const &message)
{
    std::lock_guard<std::mutex> lock(consoleMutex);
    if (consoleLines.size() == 256)
        consoleLines.pop_front();
    consoleLines.push_back({++consoleCursor, std::string(message.message, std::min<size_t>(strlen(message.message), 2048))});
}

sjson_node *consoleOutput(sjson_context *ctx, uint64_t after, int count)
{
    std::lock_guard<std::mutex> lock(consoleMutex);
    auto const out = sjson_mkobject(ctx);
    auto const lines = sjson_put_array(ctx, out, "lines");
    if (!after && consoleLines.size() > size_t(count))
        after = consoleLines[consoleLines.size() - count].id - 1;
    for (auto const &line : consoleLines)
    {
        if (line.id <= after)
            continue;
        sjson_append_element(lines, sjson_mkstring(ctx, line.text.c_str()));
        after = line.id;
        if (--count == 0)
            break;
    }
    sjson_put_double(ctx, out, "cursor", after);
    sjson_put_double(ctx, out, "latest", consoleCursor);
    return out;
}

sjson_node *gameStatus(sjson_context *ctx, bool native)
{
    auto const out = sjson_mkobject(ctx);
    auto const workspace = GetWorkspace();
    sjson_put_bool(ctx, out, "headless", g_forceHeadless);
    sjson_put_string(ctx, out, "mode", native ? "build_tools" : workspace.playing ? "playtest" :
                    workspace.board ? "editor" : ud.gm & MODE_GAME ? "game" : "menu");
    sjson_put_bool(ctx, out, "menu", bool(ud.gm & MODE_MENU));
    sjson_put_int(ctx, out, "width", xdim);
    sjson_put_int(ctx, out, "height", ydim);
    if ((ud.gm & MODE_GAME) && !native)
    {
        int const level = ud.volume_number * MAXLEVELS + ud.level_number;
        sjson_put_int(ctx, out, "episode", ud.volume_number + 1);
        sjson_put_int(ctx, out, "level", ud.level_number + 1);
        if (unsigned(level) < ARRAY_SIZE(g_mapInfo) && g_mapInfo[level].name)
            sjson_put_string(ctx, out, "level_name", g_mapInfo[level].name);
        if (boardfilename[0])
            sjson_put_string(ctx, out, "user_map", boardfilename);
        auto const player = g_player[myconnectindex].ps;
        if (player)
        {
            auto const loc = sjson_put_obj(ctx, out, "player");
            putPoint(ctx, loc, player->pos.xy);
            sjson_put_int(ctx, loc, "z", player->pos.z);
            sjson_put_int(ctx, loc, "angle", fix16_to_int(player->q16ang));
            sjson_put_int(ctx, loc, "sector", player->cursectnum);
            if (unsigned(player->i) < MAXSPRITES)
                sjson_put_int(ctx, loc, "health", sprite[player->i].extra);
        }
    }
    return out;
}

sjson_node *serverStatus(sjson_context *ctx)
{
    auto const out = gameStatus(ctx, false);
    bool const multiplayer = numplayers > 1 || ud.multimode > 1;
    bool const host = myconnectindex == connecthead;
    sjson_put_string(ctx, out, "role", g_netServerConsole ? "dedicated" : !multiplayer ? "offline" : host ? "host" : "client");
    sjson_put_int(ctx, out, "local_slot", myconnectindex);
    sjson_put_int(ctx, out, "port", mmulti_getport());
    sjson_put_int(ctx, out, "slots", numplayers);
    sjson_put_bool(ctx, out, "late_join", g_netLateJoin);
    sjson_put_bool(ctx, out, "asynchronous", g_netAsyncMode);
    sjson_put_bool(ctx, out, "changing_map", Net_ServerLevelChangePending());
    auto const roster = sjson_put_array(ctx, out, "players");
    for (int i = 0; i < numplayers && i < MAXPLAYERS; ++i)
        if (g_player[i].connected)
        {
            auto const item = sjson_mkobject(ctx);
            sjson_put_int(ctx, item, "slot", i);
            sjson_put_string(ctx, item, "name", g_player[i].user_name);
            sjson_put_bool(ctx, item, "host", i == connecthead);
            sjson_put_bool(ctx, item, "spectator", i == connecthead && g_netServerDedicated);
            sjson_put_bool(ctx, item, "bot", g_player[i].isBot);
            sjson_append_element(roster, item);
        }
    return out;
}

sjson_node *projectStatus(sjson_context *ctx, int index, int count)
{
    auto const &workspace = GetProjectWorkspace();
    auto const out = sjson_mkobject(ctx);
    sjson_put_string(ctx, out, "root", workspace.root.c_str());
    sjson_put_string(ctx, out, "name", workspace.project.name.c_str());
    sjson_put_string(ctx, out, "entry", workspace.project.entry.c_str());
    sjson_put_string(ctx, out, "target", Target(workspace.project.target).id);
    sjson_put_bool(ctx, out, "modern", workspace.project.modern);
    sjson_put_bool(ctx, out, "requires_vibebuild32", workspace.project.modern || workspace.modern);
    if (workspace.project.modern || workspace.modern)
        sjson_put_string(ctx, out, "compatibility_warning", modernCompatibilityWarning);
    auto const map = sjson_put_obj(ctx, out, "map");
    sjson_put_string(ctx, map, "target", Target(workspace.map.target).id);
    sjson_put_string(ctx, map, "effective_target", Target(workspace.effectiveTarget).id);
    sjson_put_int(ctx, map, "modern", workspace.map.modern);
    sjson_put_bool(ctx, map, "effective_modern", workspace.modern);
    auto const attachments = sjson_put_array(ctx, map, "scripts");
    for (auto const &script : workspace.map.scripts)
    {
        auto const binding = sjson_mkobject(ctx);
        sjson_put_string(ctx, binding, "kind", script.kind.c_str());
        sjson_put_string(ctx, binding, "script", script.script.c_str());
        sjson_put_int(ctx, binding, "index", script.index);
        sjson_append_element(attachments, binding);
    }
    sjson_put_int(ctx, out, "asset_count", workspace.assets.size());
    auto const assets = sjson_put_array(ctx, out, "assets");
    for (int i = index; i < std::min(index + count, int(workspace.assets.size())); ++i)
    {
        auto const &asset = workspace.assets[i];
        auto const item = sjson_mkobject(ctx);
        sjson_put_string(ctx, item, "path", asset.asset.path.c_str());
        sjson_put_bool(ctx, item, "directory", asset.asset.directory);
        sjson_put_string(ctx, item, "target", Target(asset.target).id);
        sjson_put_string(ctx, item, "icon", Target(asset.target).icon);
        sjson_put_string(ctx, item, "type", asset.asset.directory ? "folder" :
            IsPrefabAsset(asset.asset.path) ? "prefab" : IsScriptAsset(asset.asset.path) ? "script" :
            IsMapAsset(asset.asset.path) ? "map" : "file");
        sjson_append_element(assets, item);
    }
    return out;
}

sjson_node *sendInput(sjson_context *ctx, sjson_node *input, std::string &error)
{
#if defined(RENDERTYPESDL) && SDL_TARGET >= 2
    auto const events = sjson_find_member(input, "events");
    if (!events || events->tag != SJSON_ARRAY || sjson_child_count(events) < 1 || sjson_child_count(events) > 32)
    {
        error = "Supply one to 32 input events.";
        return nullptr;
    }
    auto const window = SDL_GetKeyboardFocus();
    if (!window || g_forceHeadless)
    {
        error = "Focus this game's window to send graphical input.";
        return nullptr;
    }
    auto const windowId = SDL_GetWindowID(window);
    std::vector<SDL_Event> batch;
    static uint16_t virtualModifiers;
    auto modifiers = virtualModifiers;
    sjson_node *event;
    sjson_foreach(event, events)
    {
        SDL_Event ev{};
        std::string const type = stringOf(event, "type");
        if (type == "key_down" || type == "key_up")
        {
            auto const scan = SDL_GetScancodeFromName(stringOf(event, "key"));
            if (scan == SDL_SCANCODE_UNKNOWN)
                break;
            ev.type = type == "key_down" ? SDL_KEYDOWN : SDL_KEYUP;
            ev.key.windowID = windowId;
            ev.key.state = type == "key_down" ? SDL_PRESSED : SDL_RELEASED;
            uint16_t modifier = 0;
            switch (scan)
            {
                case SDL_SCANCODE_LSHIFT: modifier = KMOD_LSHIFT; break;
                case SDL_SCANCODE_RSHIFT: modifier = KMOD_RSHIFT; break;
                case SDL_SCANCODE_LCTRL: modifier = KMOD_LCTRL; break;
                case SDL_SCANCODE_RCTRL: modifier = KMOD_RCTRL; break;
                case SDL_SCANCODE_LALT: modifier = KMOD_LALT; break;
                case SDL_SCANCODE_RALT: modifier = KMOD_RALT; break;
                case SDL_SCANCODE_LGUI: modifier = KMOD_LGUI; break;
                case SDL_SCANCODE_RGUI: modifier = KMOD_RGUI; break;
                default: break;
            }
            if (ev.type == SDL_KEYDOWN) modifiers |= modifier;
            else modifiers &= ~modifier;
            ev.key.keysym = {scan, SDL_GetKeyFromScancode(scan), uint16_t(SDL_GetModState() | modifiers), 0};
        }
        else if (type == "text")
        {
            auto const value = stringOf(event, "text");
            if (!*value || Bstrlen(value) >= sizeof(ev.text.text))
                break;
            ev.type = SDL_TEXTINPUT;
            ev.text.windowID = windowId;
            Bstrncpyz(ev.text.text, value, sizeof(ev.text.text));
        }
        else if (type == "wheel")
        {
            int y;
            if (!argument(event, "dy", -32, 32, y))
                break;
            ev.type = SDL_MOUSEWHEEL;
            ev.wheel.windowID = windowId;
            ev.wheel.y = y;
#if SDL_VERSION_ATLEAST(2, 0, 18)
            ev.wheel.preciseY = static_cast<float>(y);
#endif
        }
        else if (type == "move" || type == "button_down" || type == "button_up")
        {
            int x, y, dx = 0, dy = 0, width, height;
            SDL_GetWindowSize(window, &width, &height);
            if (!argument(event, "x", 0, width - 1, x) || !argument(event, "y", 0, height - 1, y))
                break;
            if (type == "move")
            {
                if ((sjson_find_member(event, "dx") && !argument(event, "dx", -32768, 32767, dx)) ||
                    (sjson_find_member(event, "dy") && !argument(event, "dy", -32768, 32767, dy)))
                    break;
                ev.type = SDL_MOUSEMOTION;
                ev.motion.windowID = windowId;
                ev.motion.x = x; ev.motion.y = y;
                ev.motion.xrel = dx; ev.motion.yrel = dy;
            }
            else
            {
                std::string const button = stringOf(event, "button");
                if (button != "left" && button != "right" && button != "middle")
                    break;
                ev.type = type == "button_down" ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
                ev.button.windowID = windowId;
                ev.button.button = button == "left" ? SDL_BUTTON_LEFT : button == "right" ? SDL_BUTTON_RIGHT : SDL_BUTTON_MIDDLE;
                ev.button.state = type == "button_down" ? SDL_PRESSED : SDL_RELEASED;
                ev.button.clicks = 1;
                ev.button.x = x; ev.button.y = y;
            }
        }
        else
            break;
        batch.push_back(ev);
    }
    if (batch.size() != size_t(sjson_child_count(events)))
    {
        error = "Check the event type, SDL key name, button and window coordinates.";
        return nullptr;
    }
    int queued = 0;
    for (auto &ev : batch)
        if (SDL_PushEvent(&ev) > 0)
            ++queued;
    if (queued == int(batch.size()))
        virtualModifiers = modifiers;
    auto const out = sjson_mkobject(ctx);
    sjson_put_int(ctx, out, "queued", queued);
    sjson_put_int(ctx, out, "requested", batch.size());
    return out;
#else
    UNREFERENCED_PARAMETER(ctx);
    UNREFERENCED_PARAMETER(input);
    error = "Graphical input requires SDL2.";
    return nullptr;
#endif
}

struct Properties
{
    sjson_context *ctx;
    sjson_node *out, *input;
    int artLimit;
    int recognized = 0;
    bool ok = true;

    template <typename T>
    void field(char const *name, T &value, int low, int high)
    {
        if (out)
            sjson_put_int(ctx, out, name, value);
        if (!input)
            return;
        auto const node = sjson_find_member(input, name);
        if (!node)
            return;
        ++recognized;
        int64_t n;
        if (!integer(node, low, high, n))
            ok = false;
        else
            value = n;
    }

    void sector(usectortypev7 &s)
    {
        field("floor_z", s.floorz, -(1 << 23), 1 << 23);
        field("ceiling_z", s.ceilingz, -(1 << 23), 1 << 23);
        field("floor_tile", s.floorpicnum, 0, artLimit - 1);
        field("ceiling_tile", s.ceilingpicnum, 0, artLimit - 1);
        field("floor_flags", s.floorstat, 0, 127);
        field("ceiling_flags", s.ceilingstat, 0, 127);
        field("floor_slope", s.floorheinum, INT16_MIN, INT16_MAX);
        field("ceiling_slope", s.ceilingheinum, INT16_MIN, INT16_MAX);
        field("floor_shade", s.floorshade, -128, 127);
        field("ceiling_shade", s.ceilingshade, -128, 127);
        field("floor_palette", s.floorpal, 0, 255);
        field("ceiling_palette", s.ceilingpal, 0, 255);
        field("floor_pan_x", s.floorxpanning, 0, 255);
        field("floor_pan_y", s.floorypanning, 0, 255);
        field("ceiling_pan_x", s.ceilingxpanning, 0, 255);
        field("ceiling_pan_y", s.ceilingypanning, 0, 255);
        field("visibility", s.visibility, 0, 255);
        tags(s);
        if (input && s.ceilingz > s.floorz)
            ok = false;
    }

    void wall(uwalltypev7 &w)
    {
        field("tile", w.picnum, 0, artLimit - 1);
        field("overlay_tile", w.overpicnum, -1, artLimit - 1);
        field("flags", w.cstat, 0, 1023);
        field("shade", w.shade, -128, 127);
        field("palette", w.pal, 0, 255);
        field("repeat_x", w.xrepeat, 0, 255);
        field("repeat_y", w.yrepeat, 0, 255);
        field("pan_x", w.xpanning, 0, 255);
        field("pan_y", w.ypanning, 0, 255);
        tags(w);
        if (input && (w.cstat & 48) && w.overpicnum < 0)
            w.overpicnum = w.picnum;
    }

    void sprite(uspritetypev7 &s)
    {
        field("z", s.z, -(1 << 23), 1 << 23);
        field("tile", s.picnum, 0, artLimit - 1);
        field("flags", s.cstat, 0, 0x83ff);
        field("angle", s.ang, 0, 2047);
        field("shade", s.shade, -128, 127);
        field("palette", s.pal, 0, 255);
        field("repeat_x", s.xrepeat, 0, 255);
        field("repeat_y", s.yrepeat, 0, 255);
        field("offset_x", s.xoffset, -128, 127);
        field("offset_y", s.yoffset, -128, 127);
        field("clip_distance", s.clipdist, 0, 255);
        field("status", s.statnum, 0, MAXSTATUS - 1);
        tags(s);
    }

    template<typename T>
    void tags(T &item)
    {
        field("lotag", item.lotag, INT16_MIN, INT16_MAX);
        field("hitag", item.hitag, INT16_MIN, INT16_MAX);
        field("extra", item.extra, INT16_MIN, INT16_MAX);
    }
};

bool safeDepth(std::string const &text)
{
    int depth = 0;
    bool quoted = false, escape = false;
    for (char ch : text)
    {
        if (!ch)
            return false;
        if (quoted)
        {
            if (escape) escape = false;
            else if (ch == '\\') escape = true;
            else if (ch == '"') quoted = false;
        }
        else if (ch == '"')
            quoted = true;
        else if (ch == '[' || ch == '{')
        {
            if (++depth > 64) return false;
        }
        else if (ch == ']' || ch == '}')
        {
            if (--depth < 0) return false;
        }
    }
    return depth == 0 && !quoted;
}

struct Mcp
{
    LiveSession session;
    Presence presence;
    std::string address, name;
    bool embedded = false, native = false;

    bool ready() const { return embedded ? GetWorkspace().board != nullptr : session.mode() == LiveMode::Joined; }
    Board const &draft() const { return embedded ? *GetWorkspace().board : session.board(); }
    uint64_t revision() const { return embedded ? GetWorkspace().revision : session.revision(); }
    int artLimit() const { return embedded ? GetWorkspace().tileLimit : session.artLimit(); }

    void tick()
    {
        if (!embedded)
        {
            session.tick();
            session.updatePresence(presence);
        }
    }

    sjson_node *status(sjson_context *ctx)
    {
        auto const out = sjson_mkobject(ctx);
        auto const workspace = embedded ? GetWorkspace() : WorkspaceState{};
        auto const link = embedded ? workspace.session : &session;
        auto const connection = link ? link->mode() : LiveMode::Offline;
        char const *const liveMode = connection == LiveMode::Hosting ? "hosting" : connection == LiveMode::Joined ? "joined"
            : connection == LiveMode::Connecting ? "connecting" : "offline";
        sjson_put_string(ctx, out, "state", !embedded ? liveMode : workspace.playing ? "playtest" : workspace.board ? "editing" : "closed");
        sjson_put_double(ctx, out, "revision", revision());
        sjson_put_int(ctx, out, "slot", link ? link->localSlot() : -1);
        sjson_put_int(ctx, out, "tile_limit", artLimit());
        if (embedded)
        {
            sjson_put_string(ctx, out, "session", liveMode);
            sjson_put_string(ctx, out, "name", workspace.name);
            sjson_put_string(ctx, out, "path", workspace.path);
            sjson_put_string(ctx, out, "view", workspace.perspective ? "3D" : workspace.section ? "section" : "plan");
            sjson_put_bool(ctx, out, "dirty", workspace.dirty);
            sjson_put_bool(ctx, out, "busy", workspace.busy);
            sjson_put_bool(ctx, out, "can_undo", workspace.undoable);
            sjson_put_bool(ctx, out, "can_redo", workspace.redoable);
        }
        auto const array = sjson_put_array(ctx, out, "players");
        for (int i = 0; link && i < maxEditors; ++i)
        {
            auto const &peer = link->players()[i];
            if (peer.name.empty())
                continue;
            auto const item = sjson_mkobject(ctx);
            sjson_put_int(ctx, item, "slot", i);
            sjson_put_string(ctx, item, "name", peer.name.c_str());
            sjson_put_bool(ctx, item, "ai", peer.ai);
            auto const &p = peer.presence;
            sjson_put_string(ctx, item, "activity", activityNames[int(p.activity)]);
            if (p.kind != ObjectKind::Nothing && p.revision <= link->revision())
            {
                auto const selection = sjson_put_obj(ctx, item, "selection");
                sjson_put_string(ctx, selection, "kind", objectNames[int(p.kind)]);
                if (p.kind != ObjectKind::Start)
                    sjson_put_int(ctx, selection, "index", p.index);
            }
            if (p.cursor)
            {
                auto const cursor = sjson_put_obj(ctx, item, "cursor");
                putPoint(ctx, cursor, p.position);
                sjson_put_bool(ctx, cursor, "perspective", p.perspective);
            }
            sjson_append_element(array, item);
        }
        if (!embedded)
        {
            auto const notice = session.takeNotice();
            if (!notice.empty())
                sjson_put_string(ctx, out, "notice", notice.c_str());
        }
        return out;
    }

    sjson_node *inspect(sjson_context *ctx, sjson_node *input, std::string &error)
    {
        auto const out = sjson_mkobject(ctx);
        auto const &board = draft();
        sjson_put_double(ctx, out, "revision", revision());
        sjson_put_int(ctx, out, "version", 7);
        sjson_put_int(ctx, out, "sector_count", board.sectors.size());
        sjson_put_int(ctx, out, "wall_count", board.walls.size());
        sjson_put_int(ctx, out, "sprite_count", board.sprites.size());
        auto const start = sjson_put_obj(ctx, out, "start");
        putPoint(ctx, start, {board.start.x, board.start.y});
        sjson_put_int(ctx, start, "z", board.start.z);
        sjson_put_int(ctx, start, "angle", board.angle);
        sjson_put_int(ctx, start, "sector", board.startSector);
        std::string const kind = stringOf(input, "kind");
        if (kind.empty() || kind == "map")
            return out;

        int index = 0, count = 32;
        if ((sjson_find_member(input, "index") && !argument(input, "index", 0, MAXWALLSV7, index)) ||
            (sjson_find_member(input, "count") && !argument(input, "count", 1, 80, count)))
        {
            error = "Choose a valid index and a count from 1 to 80.";
            return nullptr;
        }
        auto const data = sjson_put_array(ctx, out, kind.c_str());
        auto addRecord = [&](int i) {
            auto const item = sjson_mkobject(ctx);
            sjson_append_element(data, item);
            sjson_put_int(ctx, item, "index", i);
            return item;
        };
        if (kind == "sectors")
            for (int i = index; i < std::min(index + count, int(board.sectors.size())); ++i)
            {
                auto s = board.sectors[i];
                auto const item = addRecord(i);
                Properties{ctx, item, nullptr, artLimit()}.sector(s);
                sjson_put_int(ctx, item, "first_wall", s.wallptr);
                sjson_put_int(ctx, item, "wall_count", s.wallnum);
            }
        else if (kind == "walls")
            for (int i = index; i < std::min(index + count, int(board.walls.size())); ++i)
            {
                auto w = board.walls[i];
                auto const item = addRecord(i);
                Properties{ctx, item, nullptr, artLimit()}.wall(w);
                putPoint(ctx, item, w.xy);
                sjson_put_int(ctx, item, "point2", w.point2);
                sjson_put_int(ctx, item, "next_wall", w.nextwall);
                sjson_put_int(ctx, item, "next_sector", w.nextsector);
                sjson_put_int(ctx, item, "sector", board.wallSector(i));
            }
        else if (kind == "sprites")
            for (int i = index; i < std::min(index + count, int(board.sprites.size())); ++i)
            {
                auto s = board.sprites[i];
                auto const item = addRecord(i);
                Properties{ctx, item, nullptr, artLimit()}.sprite(s);
                putPoint(ctx, item, s.xy);
                sjson_put_int(ctx, item, "sector", s.sectnum);
            }
        else
        {
            error = "Choose map, sectors, walls or sprites.";
            return nullptr;
        }
        return out;
    }

    bool showPresence(sjson_node *input, std::string &error)
    {
        if (boolOf(input, "clear"))
        {
            presence = {};
            if (embedded)
                return PointInWorkspace({}, true, error);
            return true;
        }
        auto const &board = draft();
        std::string const kind = stringOf(input, "kind");
        Presence next = embedded ? GetWorkspace().presence : presence;
        if (sjson_find_member(input, "x") || sjson_find_member(input, "y"))
        {
            if (!pointArgument(input, next.position))
            {
                error = "Supply both x and y in Build coordinates.";
                return false;
            }
            next.cursor = true;
        }
        if (!kind.empty())
        {
            int capacity = 0;
            if (kind == "sector") { next.kind = ObjectKind::Sector; capacity = board.sectors.size(); }
            else if (kind == "wall") { next.kind = ObjectKind::Wall; capacity = board.walls.size(); }
            else if (kind == "vertex") { next.kind = ObjectKind::Vertex; capacity = board.walls.size(); }
            else if (kind == "sprite") { next.kind = ObjectKind::Sprite; capacity = board.sprites.size(); }
            else if (kind == "start") next.kind = ObjectKind::Start;
            else if (kind == "none") next.kind = ObjectKind::Nothing;
            else
            {
                error = "Unknown selection kind.";
                return false;
            }
            int index = -1;
            if (next.kind != ObjectKind::Nothing && next.kind != ObjectKind::Start &&
                !argument(input, "index", 0, capacity - 1, index))
            {
                error = "Choose an existing object index.";
                return false;
            }
            next.index = index;
            if (!embedded)
                next.revision = session.revision();
        }
        next.activity = Activity::Select;
        if (embedded)
            return PointInWorkspace(next, !kind.empty(), error);
        presence = next;
        return true;
    }

    bool applyOperation(sjson_context *ctx, sjson_node *input, Board &board, Presence &p, std::string &error)
    {
        p.activity = Activity::Select;
        std::string const operation = stringOf(input, "operation");
        std::string const kind = stringOf(input, "kind");
        int index;
        auto indexed = [&](int count) { return argument(input, "index", 0, count - 1, index); };
        auto selectAt = [&](ObjectKind kind, int index, vec2_t pos) {
            p.kind = kind; p.index = index; p.position = pos; p.cursor = true;
        };
        bool changed = false;
        if (operation == "room")
        {
            auto const points = sjson_find_member(input, "points");
            const int count = points && points->tag == SJSON_ARRAY ? sjson_child_count(points) : 0;
            if (count >= 3 && count <= MAXWALLSV7)
            {
                std::vector<vec2_t> outline;
                sjson_node *point;
                sjson_foreach(point, points)
                {
                    int64_t x, y;
                    if (point->tag != SJSON_ARRAY || sjson_child_count(point) != 2 ||
                        !integer(sjson_find_element(point, 0), -BXY_MAX, BXY_MAX, x) ||
                        !integer(sjson_find_element(point, 1), -BXY_MAX, BXY_MAX, y))
                        break;
                    outline.push_back({int(x), int(y)});
                }
                changed = int(outline.size()) == count && board.addRoom(outline);
                if (changed)
                {
                    selectAt(ObjectKind::Sector, board.sectors.size() - 1, outline.back());
                    p.activity = Activity::DrawRoom;
                }
            }
        }
        else if ((operation == "extend" || operation == "split" || operation == "move_vertex") && indexed(board.walls.size()))
        {
            selectAt(operation == "move_vertex" ? ObjectKind::Vertex : ObjectKind::Wall, index, board.walls[index].xy);
            if (operation == "extend")
            {
                int depth = 1536;
                changed = (!sjson_find_member(input, "depth") || argument(input, "depth", 128, 32768, depth)) &&
                          board.extendRoom(index, depth);
                if (changed)
                {
                    selectAt(ObjectKind::Sector, board.sectors.size() - 1, board.walls[index].xy);
                    p.activity = Activity::DrawRoom;
                }
            }
            else if (operation == "split")
            {
                int vertex;
                changed = board.splitWall(index, &vertex);
                if (changed)
                    selectAt(ObjectKind::Vertex, vertex, board.walls[vertex].xy);
            }
            else
            {
                vec2_t point;
                if (pointArgument(input, point))
                {
                    for (int vertex : board.linkedVertices({index}))
                        board.walls[vertex].xy = point;
                    p.position = point;
                    p.activity = Activity::Drag;
                    changed = true;
                }
            }
        }
        else if (operation == "sprite" || operation == "move_sprite" || operation == "start")
        {
            int preferred = -1;
            vec2_t point;
            if (pointArgument(input, point) &&
                (!sjson_find_member(input, "sector") || argument(input, "sector", 0, board.sectors.size() - 1, preferred)))
            {
                if (operation == "sprite")
                {
                    int tile;
                    changed = argument(input, "tile", 0, artLimit() - 1, tile) && board.addSprite(tile, point, preferred);
                    if (changed) selectAt(ObjectKind::Sprite, board.sprites.size() - 1, point);
                    p.activity = Activity::PlaceSprite;
                }
                else if (operation == "start")
                {
                    int angle = board.angle;
                    changed = (!sjson_find_member(input, "angle") || argument(input, "angle", 0, 2047, angle)) &&
                              board.setStart(point, preferred);
                    if (changed) { board.angle = angle; selectAt(ObjectKind::Start, 0, point); }
                    p.activity = Activity::PlaceStart;
                }
                else if (indexed(board.sprites.size()))
                {
                    changed = board.moveSprite(index, point);
                    selectAt(ObjectKind::Sprite, index, point);
                    p.activity = Activity::Drag;
                }
            }
        }
        else if (operation == "delete")
        {
            if (kind == "sector" && indexed(board.sectors.size()))
            {
                board.deleteSector(index);
                changed = true;
            }
            else if (kind == "sprite" && indexed(board.sprites.size()))
            {
                board.deleteSprite(index);
                changed = true;
            }
            p.kind = ObjectKind::Nothing;
            p.index = -1;
        }
        else if (operation == "set")
        {
            auto const fields = sjson_find_member(input, "properties");
            if (fields && fields->tag == SJSON_OBJECT)
            {
                Properties properties{ctx, nullptr, fields, artLimit()};
                if (kind == "sector" && indexed(board.sectors.size()))
                {
                    properties.sector(board.sectors[index]);
                    selectAt(ObjectKind::Sector, index, board.walls[board.sectors[index].wallptr].xy);
                }
                else if (kind == "wall" && indexed(board.walls.size()))
                {
                    properties.wall(board.walls[index]);
                    selectAt(ObjectKind::Wall, index, board.walls[index].xy);
                }
                else if (kind == "sprite" && indexed(board.sprites.size()))
                {
                    properties.sprite(board.sprites[index]);
                    selectAt(ObjectKind::Sprite, index, board.sprites[index].xy);
                }
                else if (kind == "start" && board.startSector >= 0)
                {
                    vec2_t const pos{board.start.x, board.start.y};
                    properties.field("angle", board.angle, 0, 2047);
                    properties.field("z", board.start.z, board.ceilingAt(board.startSector, pos), board.floorAt(board.startSector, pos));
                    selectAt(ObjectKind::Start, 0, pos);
                }
                changed = properties.ok && properties.recognized > 0 && properties.recognized == sjson_child_count(fields);
            }
        }
        if (!changed)
        {
            error = "The edit could not be applied. Check the operation, indexes, property ranges or outline.";
            return false;
        }
        return true;
    }

    bool edit(sjson_context *ctx, sjson_node *input, std::string &error)
    {
        int64_t rev;
        if (!integer(sjson_find_member(input, "revision"), 1, 9007199254740991LL, rev) || uint64_t(rev) != revision())
        {
            error = "Inspect the current revision before editing.";
            return false;
        }
        auto const workspace = embedded ? GetWorkspace() : WorkspaceState{};
        if (workspace.playing || workspace.busy || session.pending())
        {
            error = "The host is still processing the previous edit.";
            return false;
        }
        auto const &before = draft();
        Board board = before;
        auto p = embedded ? workspace.presence : presence;
        std::string const operation = stringOf(input, "operation");
        auto const batch = operation == "batch";
        std::string label = stringOf(input, "label");
        if (label.size() > 64)
        {
            error = "Use a change label up to 64 characters.";
            return false;
        }
        if (batch)
        {
            auto const edits = sjson_find_member(input, "edits");
            int const count = edits && edits->tag == SJSON_ARRAY ? sjson_child_count(edits) : 0;
            if (count < 1 || count > 64)
            {
                error = "A batch needs between 1 and 64 edit operations.";
                return false;
            }
            if (label.empty()) label = "Batch edit (" + std::to_string(count) + " operations)";
            int i = 0;
            sjson_node *item;
            sjson_foreach(item, edits)
            {
                ++i;
                auto const previous = board;
                if (item->tag != SJSON_OBJECT || !applyOperation(ctx, item, board, p, error) ||
                    !board.validate(artLimit(), error, true) || !board.validateGeometryChange(previous, error))
                {
                    error = "Operation " + std::to_string(i) + " failed. " + error + " The batch was discarded.";
                    return false;
                }
            }
        }
        else
        {
            if (label.empty()) label = "Editor edit (" + operation + ")";
            if (sjson_find_member(input, "edits"))
            {
                error = "Use the batch operation with an edits array.";
                return false;
            }
            if (!applyOperation(ctx, input, board, p, error))
                return false;
        }
        if (!board.validate(artLimit(), error, true) || !board.validateGeometryChange(before, error))
            return false;
        if (embedded)
            return EditWorkspace(rev, std::move(board), p, error, label, batch);
        if (!session.submit(rev, board, label, batch, true))
        {
            error = session.takeNotice();
            if (error.empty()) error = "Could not send the edit.";
            return false;
        }
        auto const deadline = Clock::now() + std::chrono::seconds(4);
        while (session.pending() && Clock::now() < deadline)
        {
            tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        error = session.takeNotice();
        if (!error.empty())
            return false;
        if (!ready() || session.pending())
        {
            error = "The host has not confirmed the edit. Check the current revision before retrying.";
            return false;
        }
        if (!SameTopology(board, session.board()))
        {
            p.kind = ObjectKind::Nothing;
            p.index = -1;
        }
        p.revision = session.revision();
        presence = p;
        session.updatePresence(presence);
        return true;
    }

    sjson_node *history(sjson_context *ctx, sjson_node *input, std::string &error)
    {
        int count = 8;
        if (sjson_find_member(input, "count") && !argument(input, "count", 1, 24, count))
        {
            error = "Choose a history count from 1 to 24.";
            return nullptr;
        }
        auto const records = embedded ? GetWorkspace().changes : &session.changes();
        auto const out = sjson_mkobject(ctx);
        auto const list = sjson_put_array(ctx, out, "changes");
        if (!records)
            return out;
        for (auto it = records->rbegin(); it != records->rend() && count-- > 0; ++it)
        {
            auto const item = sjson_mkobject(ctx);
            sjson_append_element(list, item);
            sjson_put_double(ctx, item, "revision", it->revision);
            sjson_put_int(ctx, item, "slot", it->slot);
            sjson_put_string(ctx, item, "author", it->author.c_str());
            sjson_put_string(ctx, item, "label", it->label.c_str());
            sjson_put_bool(ctx, item, "ai", it->ai);
            sjson_put_bool(ctx, item, "structure_changed", it->structure);
            auto addRefs = [&](const char *key, std::vector<ObjectRef> const &refs) {
                auto const entries = sjson_put_array(ctx, item, key);
                for (size_t i = 0; i < std::min(refs.size(), size_t(80)); ++i)
                {
                    auto const object = sjson_mkobject(ctx);
                    sjson_put_string(ctx, object, "kind", objectNames[int(refs[i].kind)]);
                    sjson_put_int(ctx, object, "index", refs[i].index);
                    sjson_append_element(entries, object);
                }
            };
            sjson_put_int(ctx, item, "touched_count", it->touched.size());
            sjson_put_int(ctx, item, "removed_count", it->removed.size());
            addRefs("touched", it->touched);
            addRefs("removed", it->removed);
        }
        return out;
    }

    sjson_node *call(sjson_context *ctx, sjson_node *params, std::string &error)
    {
        std::string const tool = stringOf(params, "name");
        auto const args = sjson_find_member(params, "arguments");
        if (embedded)
        {
            if (tool == "game_status") return gameStatus(ctx, native);
            if (tool == "server_status" && !native) return serverStatus(ctx);
            if (tool == "ui_input") return sendInput(ctx, args, error);
            if (tool == "console_output")
            {
                int count = 32;
                int64_t after = 0;
                if ((sjson_find_member(args, "limit") && !argument(args, "limit", 1, 80, count)) ||
                    (sjson_find_member(args, "after") && !integer(sjson_find_member(args, "after"), 0, 9007199254740991LL, after)))
                {
                    error = "Supply a valid console cursor and a limit from 1 to 80.";
                    return nullptr;
                }
                return consoleOutput(ctx, after, count);
            }
            if (tool == "console_execute")
            {
                auto const command = stringOf(args, "command");
                if (!*command || Bstrlen(command) > 4096 || strcspn(command, "\r\n") != Bstrlen(command))
                {
                    error = "Supply one console line of up to 4096 bytes.";
                    return nullptr;
                }
                uint64_t after;
                {
                    std::lock_guard<std::mutex> lock(consoleMutex);
                    after = consoleCursor;
                }
                OSD_Dispatch(command, true);
                return consoleOutput(ctx, after, 80);
            }
            if (tool == "project_status" && !native)
            {
                int index = 0, count = 40;
                if ((sjson_find_member(args, "index") && !argument(args, "index", 0, 16384, index)) ||
                    (sjson_find_member(args, "count") && !argument(args, "count", 1, 80, count)))
                {
                    error = "Choose an asset index and a count from 1 to 80.";
                    return nullptr;
                }
                return projectStatus(ctx, index, count);
            }
            if (tool == "project_control" && !native)
            {
                static char const *const actions[] = {"create", "open", "close", "import_zip", "export_zip", "add_asset",
                    "settings", "map_settings", "attach", "detach", "export_map", "create_component", "save_prefab", "place_prefab"};
                auto const action = stringOf(args, "action");
                size_t i = 0;
                while (i < ARRAY_SIZE(actions) && Bstrcmp(action, actions[i])) ++i;
                if (i == ARRAY_SIZE(actions))
                {
                    error = "Choose a project action.";
                    return nullptr;
                }
                ProjectRequest request{static_cast<ProjectAction>(i)};
                request.path = stringOf(args, "path");
                request.destination = stringOf(args, "destination");
                request.name = stringOf(args, "name");
                request.entry = stringOf(args, "entry");
                request.setEntry = sjson_find_member(args, "entry") != nullptr;
                request.script = stringOf(args, "script");
                request.kind = stringOf(args, "kind");
                request.overwrite = boolOf(args, "overwrite");
                auto const target = stringOf(args, "target");
                request.target = ParseTarget(target);
                request.setTarget = *target != 0;
                std::string const modern = stringOf(args, "modern");
                if ((request.setTarget && request.target == GameTarget::Inherit && Bstrcmp(target, "inherit")) ||
                    (!modern.empty() && modern != "inherit" && modern != "off" && modern != "on") ||
                    (sjson_find_member(args, "index") && !argument(args, "index", -1, 8191, request.index)))
                {
                    error = "Choose a supported game target, modern setting and object index.";
                    return nullptr;
                }
                if (!modern.empty()) request.modern = modern == "inherit" ? -1 : modern == "on" ? 1 : 0;
                if (auto const sprites = sjson_find_member(args, "sprites"))
                {
                    if (request.action != ProjectAction::SavePrefab || sprites->tag != SJSON_ARRAY ||
                        sjson_child_count(sprites) < 1 || sjson_child_count(sprites) > MAXSPRITESV7)
                    {
                        error = "Supply sprite indexes for the prefab selection.";
                        return nullptr;
                    }
                    sjson_node *spriteIndex;
                    sjson_foreach(spriteIndex, sprites)
                    {
                        int64_t index;
                        if (!integer(spriteIndex, 0, MAXSPRITESV7 - 1, index))
                        {
                            error = "Every prefab selection index must refer to a sprite.";
                            return nullptr;
                        }
                        request.sprites.push_back(index);
                    }
                }
                if (request.action == ProjectAction::PlacePrefab)
                {
                    request.useFloorLayer = sjson_find_member(args, "floor_z") != nullptr;
                    if (!pointArgument(args, request.position) ||
                        (sjson_find_member(args, "sector") && !argument(args, "sector", 0, MAXSECTORSV7 - 1, request.sector)) ||
                        (request.useFloorLayer && !argument(args, "floor_z", INT32_MIN, INT32_MAX, request.floorLayer)))
                    {
                        error = "Supply valid prefab coordinates and optional sector and floor Z.";
                        return nullptr;
                    }
                }
                return ControlProject(request, error) ? projectStatus(ctx, 0, 40) : nullptr;
            }
            if (tool == "editor_control" && !native)
            {
                static char const *const actions[] = {"enter", "new", "open", "save", "undo", "redo", "playtest",
                    "stop_playtest", "close", "plan", "perspective", "frame", "host", "join", "leave", "build_tools", "section"};
                auto const action = stringOf(args, "action");
                size_t i = 0;
                while (i < ARRAY_SIZE(actions) && Bstrcmp(action, actions[i]))
                    ++i;
                if (i == ARRAY_SIZE(actions))
                {
                    error = "Choose a valid editor action.";
                    return nullptr;
                }
                WorkspaceRequest request{};
                request.action = static_cast<WorkspaceAction>(i);
                request.path = stringOf(args, "path");
                request.address = stringOf(args, "address");
                request.name = stringOf(args, "name");
                auto const target = stringOf(args, "target");
                request.target = ParseTarget(target);
                if (*target && Bstrcmp(target, "inherit") && request.target == GameTarget::Inherit)
                {
                    error = "Choose a supported game target.";
                    return nullptr;
                }
                request.discard = boolOf(args, "discard");
                request.overwrite = boolOf(args, "overwrite");
                if (sjson_find_member(args, "port") && !argument(args, "port", 1, UINT16_MAX, request.port))
                {
                    error = "Supply a port from 1 to 65535.";
                    return nullptr;
                }
                if (request.action == WorkspaceAction::Play)
                {
                    auto const spawn = sjson_find_member(args, "spawn");
                    auto const coordinates = sjson_find_member(args, "x") || sjson_find_member(args, "y") ||
                        sjson_find_member(args, "sector") || sjson_find_member(args, "angle") || sjson_find_member(args, "platform");
                    if (spawn || coordinates)
                    {
                        PlayPoint point;
                        if ((spawn && (coordinates || !argument(args, "spawn", 0, MAXSPRITESV7 - 1, point.spawn))) ||
                            (!spawn && (!pointArgument(args, point.xy) || !argument(args, "sector", 0, MAXSECTORSV7 - 1, point.sector))) ||
                            (sjson_find_member(args, "angle") && !argument(args, "angle", 0, 2047, point.angle)) ||
                            (sjson_find_member(args, "platform") && !argument(args, "platform", 0, MAXSPRITESV7 - 1, point.platform)))
                        {
                            error = "Choose a spawn sprite, or supply x, y and a sector for a temporary start.";
                            return nullptr;
                        }
                        request.playFrom = point;
                    }
                }
                return ControlWorkspace(request, error) ? status(ctx) : nullptr;
            }
        }
        if (tool == "editor_status")
            return status(ctx);
        if (!embedded && tool == "editor_disconnect")
        {
            session.leave();
            presence = {};
            return status(ctx);
        }
        if (!embedded && tool == "editor_connect")
        {
            auto const requestedAddress = stringOf(args, "address");
            auto const requestedName = stringOf(args, "name");
            presence = {};
            if (!session.join(*requestedAddress ? requestedAddress : address, *requestedName ? requestedName : name, 0, true))
            {
                error = session.takeNotice();
                return nullptr;
            }
            return status(ctx);
        }
        if (!ready())
        {
            error = embedded ? "Open the integrated editor using editor_control." :
                               "The adapter has not joined a live editor. Check editor_status.";
            return nullptr;
        }
        if (tool == "editor_inspect")
            return inspect(ctx, args, error);
        if (tool == "editor_check" && embedded)
        {
            int index = 0, count = 40;
            if ((sjson_find_member(args, "index") && !argument(args, "index", 0, INT_MAX - 80, index)) ||
                (sjson_find_member(args, "count") && !argument(args, "count", 1, 80, count)))
            {
                error = "Choose a valid finding index and count.";
                return nullptr;
            }
            auto const issues = CheckWorkspaceMap();
            auto const out = sjson_mkobject(ctx);
            sjson_put_int(ctx, out, "finding_count", issues.size());
            auto const list = sjson_put_array(ctx, out, "findings");
            for (int i = index; i < std::min(index + count, int(issues.size())); ++i)
            {
                auto const item = sjson_mkobject(ctx);
                auto const &issue = issues[i];
                sjson_put_string(ctx, item, "kind", objectNames[int(issue.object.kind)]);
                sjson_put_int(ctx, item, "index", issue.object.index);
                sjson_put_string(ctx, item, "message", issue.message.c_str());
                sjson_append_element(list, item);
            }
            return out;
        }
        if (tool == "editor_history")
            return history(ctx, args, error);
        if (tool == "editor_presence" && showPresence(args, error))
            return status(ctx);
        if (tool == "editor_edit" && edit(ctx, args, error))
            return status(ctx);
        if (tool == "editor_export")
        {
            std::string const path = stringOf(args, "path");
            auto const dot = Bstrrchr(path.c_str(), '.');
            if (!dot || Bstrcasecmp(dot, ".map"))
                error = "Choose a local filename ending in .map.";
            else if (buildvfs_exists(path.c_str()) && !boolOf(args, "overwrite"))
                error = "The destination already exists. Choose another path or request overwrite.";
            else if (WriteBoard(path, draft(), artLimit(), error))
            {
                auto const out = status(ctx);
                sjson_put_string(ctx, out, "saved", path.c_str());
                return out;
            }
        }
        if (error.empty())
            error = "Unknown editor tool.";
        return nullptr;
    }

    sjson_node *listTools(sjson_context *ctx)
    {
        auto const description = sjson_decode(ctx, tools);
        if (!embedded)
            return description;
        auto const list = sjson_find_member(description, "tools");
        for (auto tool = sjson_first_child(list); tool;)
        {
            auto const next = tool->next;
            auto const name = stringOf(tool, "name");
            if (native || !Bstrcmp(name, "editor_connect") || !Bstrcmp(name, "editor_disconnect"))
                sjson_delete_node(ctx, tool);
            tool = next;
        }
        auto append = [&](char const *json) {
            auto const additions = sjson_decode(ctx, json);
            while (auto const tool = sjson_first_child(additions))
            {
                sjson_remove_from_parent(tool);
                if (native && !Bstrcmp(stringOf(tool, "name"), "server_status"))
                    sjson_delete_node(ctx, tool);
                else
                    sjson_append_element(list, tool);
            }
            sjson_delete_node(ctx, additions);
        };
        append(gameTools);
        if (!native)
            append(workspaceTools);
        return description;
    }

    std::string respond(std::string const &request)
    {
        auto const ctx = sjson_create_context(0, 0, nullptr);
        auto const root = safeDepth(request) ? sjson_decode(ctx, request.c_str()) : nullptr;
        auto const id = sjson_find_member(root, "id");
        auto const method = stringOf(root, "method");
        bool const valid = root && root->tag == SJSON_OBJECT && !Bstrcmp(stringOf(root, "jsonrpc"), "2.0") && *method &&
            (!id || id->tag == SJSON_STRING || (id->tag == SJSON_NUMBER && std::isfinite(id->number_)));
        if (valid && !id)
        {
            sjson_destroy_context(ctx);
            return {};
        }
        auto const out = sjson_mkobject(ctx);
        sjson_put_string(ctx, out, "jsonrpc", "2.0");
        auto const responseId = id && id->tag == SJSON_STRING ? sjson_mkstring(ctx, id->string_) :
            id && id->tag == SJSON_NUMBER ? sjson_mknumber(ctx, id->number_) : sjson_mknull(ctx);
        sjson_append_member(ctx, out, "id", responseId);
        auto const params = sjson_find_member(root, "params");
        auto rpcError = [&](int code, char const *msg) {
            auto const item = sjson_put_obj(ctx, out, "error");
            sjson_put_int(ctx, item, "code", code);
            sjson_put_string(ctx, item, "message", msg);
        };
        if (!root)
            rpcError(-32700, "Parse error");
        else if (!valid)
            rpcError(-32600, "Invalid request");
        else if (!Bstrcmp(method, "initialize"))
        {
            auto const result = sjson_put_obj(ctx, out, "result");
            std::string const offered = stringOf(params, "protocolVersion");
            auto const version = offered == "2024-11-05" || offered == "2025-03-26" || offered == "2025-06-18"
                ? offered.c_str() : "2025-11-25";
            sjson_put_string(ctx, result, "protocolVersion", version);
            auto const caps = sjson_put_obj(ctx, result, "capabilities");
            auto const tools = sjson_put_obj(ctx, caps, "tools");
            sjson_put_bool(ctx, tools, "listChanged", false);
            auto const info = sjson_put_obj(ctx, result, "serverInfo");
            sjson_put_string(ctx, info, "name", "vibebuild32-map-editor");
            sjson_put_string(ctx, info, "version", "1.0");
            sjson_put_string(ctx, result, "instructions", embedded ?
                "Use game_status and tools/list to discover the active workspace. Inspect the map revision before editing." :
                "Inspect the map revision and set a visible editor presence before editing.");
        }
        else if (!Bstrcmp(method, "ping"))
            sjson_put_obj(ctx, out, "result");
        else if (!Bstrcmp(method, "tools/list"))
            sjson_append_member(ctx, out, "result", listTools(ctx));
        else if (!Bstrcmp(method, "tools/call"))
        {
            std::string error;
            auto const data = call(ctx, params, error);
            auto const result = sjson_put_obj(ctx, out, "result");
            sjson_put_bool(ctx, result, "isError", data == nullptr);
            auto const content = sjson_put_array(ctx, result, "content");
            auto const part = sjson_mkobject(ctx);
            sjson_put_string(ctx, part, "type", "text");
            if (data)
            {
                auto const encoded = sjson_stringify(ctx, data, nullptr);
                sjson_put_string(ctx, part, "text", encoded);
                sjson_free_string(ctx, encoded);
                sjson_append_member(ctx, result, "structuredContent", data);
            }
            else
                sjson_put_string(ctx, part, "text", error.c_str());
            sjson_append_element(content, part);
        }
        else
            rpcError(-32601, "Method not found");

        auto const encoded = sjson_stringify(ctx, out, nullptr);
        std::string result = encoded;
        sjson_free_string(ctx, encoded);
        sjson_destroy_context(ctx);
        return result;
    }
};

struct StdioInput
{
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<std::string> requests;
    bool eof = false, stopping = false;

    void read()
    {
        std::string line;
        bool oversized = false;
        for (int c; (c = getchar()) != EOF;)
        {
            if (c == '\n')
            {
                std::unique_lock<std::mutex> lock(mutex);
                changed.wait(lock, [&] { return stopping || requests.size() < 16; });
                if (stopping)
                    return;
                requests.push_back(oversized ? std::string{} : std::move(line));
                line.clear();
                oversized = false;
                changed.notify_all();
            }
            else if (line.size() < maxRequestBytes && !oversized)
                line.push_back(static_cast<char>(c));
            else
                oversized = true;
        }
        std::lock_guard<std::mutex> lock(mutex);
        eof = true;
        changed.notify_all();
    }

    bool next(std::string &request, bool wait, bool &ended)
    {
        std::unique_lock<std::mutex> lock(mutex);
        if (wait)
            changed.wait_for(lock, std::chrono::milliseconds(12), [&] { return eof || !requests.empty(); });
        if (requests.empty())
        {
            ended = eof;
            return false;
        }
        request = std::move(requests.front());
        requests.pop_front();
        changed.notify_all();
        return true;
    }

    void stop()
    {
        std::lock_guard<std::mutex> lock(mutex);
        stopping = true;
        changed.notify_all();
    }
};

std::shared_ptr<StdioInput> startStdioInput()
{
    auto input = std::make_shared<StdioInput>();
    // A GUI can close while its MCP client still has stdin open. The reader
    // holds only the input queue and never accesses game or editor state.
    std::thread([input] { input->read(); }).detach();
    return input;
}

void writeStdioReply(FILE *output, Mcp &mcp, std::string const &request)
{
    auto const reply = mcp.respond(request);
    if (!reply.empty())
    {
        fwrite(reply.data(), 1, reply.size(), output);
        fputc('\n', output);
        fflush(output);
    }
}

int stdioServer(Mcp &mcp)
{
    auto const input = startStdioInput();
    bool ended = false;
    while (!ended)
    {
        mcp.tick();
        std::string request;
        if (input->next(request, true, ended))
            writeStdioReply(stdout, mcp, request);
    }
    return EXIT_SUCCESS;
}

std::string httpReply(char const *status, std::string body = {}, char const *extra = "")
{
    return std::string("HTTP/1.1 ") + status + "\r\nContent-Type: application/json\r\nContent-Length: " +
           std::to_string(body.size()) + "\r\nConnection: close\r\n" + extra + "\r\n" + body;
}

std::string header(std::string const &text, char const *name)
{
    size_t line = text.find("\r\n");
    while (line != std::string::npos && line + 2 < text.size())
    {
        line += 2;
        auto const end = text.find("\r\n", line);
        auto const length = strlen(name);
        if (end == line)
            break;
        if (end != std::string::npos && line + length < end && text[line + length] == ':' &&
            !Bstrncasecmp(text.c_str() + line, name, length))
        {
            auto const value = text.find_first_not_of(" \t", line + length + 1);
            return value < end ? text.substr(value, end - value) : "";
        }
        line = end;
    }
    return {};
}

struct HttpConnection
{
    ENetSocket socket;
    Clock::time_point deadline;
    std::string input, output;
    size_t sent = 0;
};

void httpRequest(Mcp &mcp, HttpConnection &conn, int port)
{
    auto const separator = conn.input.find("\r\n\r\n");
    if (separator == std::string::npos && conn.input.size() <= maxHeaders)
        return;
    if (separator > maxHeaders)
    {
        conn.output = httpReply("431 Request Header Fields Too Large");
        return;
    }
    auto const headers = conn.input.substr(0, separator + 4);
    auto const origin = header(headers, "Origin");
    auto const configuredOrigin = getenv("VIBEBUILD_MCP_ORIGIN");
    if (!origin.empty() && origin != "http://127.0.0.1:" + std::to_string(port) &&
        origin != "http://localhost:" + std::to_string(port) && !(configuredOrigin && origin == configuredOrigin))
    {
        conn.output = httpReply("403 Forbidden");
        return;
    }
    auto const token = getenv("VIBEBUILD_MCP_TOKEN");
    if (token && *token && header(headers, "Authorization") != std::string("Bearer ") + token)
    {
        conn.output = httpReply("401 Unauthorized", {}, "WWW-Authenticate: Bearer\r\n");
        return;
    }
    if (conn.input.compare(0, 10, "POST /mcp ") != 0)
    {
        conn.output = conn.input.compare(0, 9, "GET /mcp ") == 0
            ? httpReply("405 Method Not Allowed", {}, "Allow: POST\r\n") : httpReply("404 Not Found");
        return;
    }
    auto const lengthText = header(headers, "Content-Length");
    char *end;
    auto const length = strtol(lengthText.c_str(), &end, 10);
    if (lengthText.empty() || *end || length < 1 || size_t(length) > maxRequestBytes ||
        !header(headers, "Transfer-Encoding").empty())
    {
        conn.output = httpReply("400 Bad Request");
        return;
    }
    const auto offset = separator + 4;
    if (conn.input.size() < offset + length)
        return;
    auto body = mcp.respond(conn.input.substr(offset, length));
    conn.output = body.empty() ? httpReply("202 Accepted") : httpReply("200 OK", std::move(body));
}

void closeOnExec(ENetSocket socket)
{
#ifdef _WIN32
    SetHandleInformation(reinterpret_cast<HANDLE>(socket), HANDLE_FLAG_INHERIT, 0);
#else
    fcntl(socket, F_SETFD, FD_CLOEXEC);
#endif
}

class HttpTransport
{
    ENetSocket listener = ENET_SOCKET_NULL;
    int port = 0;
    std::vector<HttpConnection> clients;

public:
    ~HttpTransport()
    {
        for (auto const &client : clients)
            enet_socket_destroy(client.socket);
        if (listener != ENET_SOCKET_NULL)
            enet_socket_destroy(listener);
    }

    bool start(int listenPort)
    {
        if (!InitializeEditorNetwork())
            return false;
        sockaddr_in endpoint{};
        endpoint.sin_family = AF_INET;
        endpoint.sin_port = htons(listenPort);
        endpoint.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener == ENET_SOCKET_NULL)
            return false;
        closeOnExec(listener);
        enet_socket_set_option(listener, ENET_SOCKOPT_REUSEADDR, 1);
        enet_socket_set_option(listener, ENET_SOCKOPT_NONBLOCK, 1);
        if (bind(listener, reinterpret_cast<sockaddr *>(&endpoint), sizeof(endpoint)) < 0 ||
            enet_socket_listen(listener, 8) < 0)
        {
            enet_socket_destroy(listener);
            listener = ENET_SOCKET_NULL;
            return false;
        }
        port = listenPort;
        fprintf(stderr, "VibeBuild32 MCP listening at http://127.0.0.1:%d/mcp\n", port);
        return true;
    }

    void poll(Mcp &mcp)
    {
        if (listener == ENET_SOCKET_NULL)
            return;
        if (clients.size() < 8)
        {
            auto const socket = enet_socket_accept(listener, nullptr);
            if (socket != ENET_SOCKET_NULL)
            {
                closeOnExec(socket);
                enet_socket_set_option(socket, ENET_SOCKOPT_NONBLOCK, 1);
                clients.push_back({socket, Clock::now() + std::chrono::seconds(8), {}, {}, 0});
            }
        }
        for (auto client = clients.begin(); client != clients.end();)
        {
            bool done = Clock::now() >= client->deadline;
            if (!done && client->output.empty())
            {
                char buffer[8192];
                ENetBuffer data;
                data.data = buffer;
                data.dataLength = sizeof(buffer);
                auto const count = enet_socket_receive(client->socket, nullptr, &data, 1);
                done = count < 0;
                if (count > 0)
                {
                    client->input.append(buffer, count);
                    httpRequest(mcp, *client, port);
                }
            }
            if (!done && !client->output.empty())
            {
                ENetBuffer data;
                data.data = &client->output[client->sent];
                data.dataLength = client->output.size() - client->sent;
                auto const sent = enet_socket_send(client->socket, nullptr, &data, 1);
                if (sent >= 0) client->sent += sent;
                done = sent < 0 || client->sent == client->output.size();
            }
            if (done)
            {
                enet_socket_destroy(client->socket);
                client = clients.erase(client);
            }
            else
                ++client;
        }
    }
};

int httpServer(Mcp &mcp, int port)
{
    HttpTransport http;
    if (!http.start(port))
    {
        fprintf(stderr, "Could not start the local MCP HTTP endpoint.\n");
        return EXIT_FAILURE;
    }
    while (true)
    {
        mcp.tick();
        http.poll(mcp);
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
}

class GameTransport
{
public:
    Mcp control;
    HttpTransport http;
    FILE *response = nullptr;
    bool open = false;
    std::shared_ptr<StdioInput> input;
    bool polling = false;
    bool stdio = false;
    int httpPort = 0;

    ~GameTransport()
    {
        if (input)
            input->stop();
        if (open)
            loguru::remove_callback("VibeBuild32 MCP");
        if (response)
            fclose(response);
    }

    bool reserveStdout()
    {
        fflush(stdout);
#ifdef _WIN32
        int const fd = _dup(_fileno(stdout));
        if (fd < 0)
            return false;
        response = _fdopen(fd, "wb");
        return response && _dup2(_fileno(stderr), _fileno(stdout)) == 0;
#else
        int const fd = dup(STDOUT_FILENO);
        if (fd < 0)
            return false;
        fcntl(fd, F_SETFD, FD_CLOEXEC);
        signal(SIGPIPE, SIG_IGN);
        response = fdopen(fd, "w");
        return response && dup2(STDERR_FILENO, STDOUT_FILENO) >= 0;
#endif
    }

    void pollStdin()
    {
        std::string request;
        bool ended = false;
        for (int i = 0; i < 4 && input->next(request, false, ended); ++i)
            writeStdioReply(response, control, request);
    }

};

std::unique_ptr<GameTransport> gameTransport;
std::vector<const char *> gameArgs;

} // namespace

int MapEditor_RunMcp(int argc, const char *const *argv)
{
    std::string address = "127.0.0.1", name = "AI Editor";
    int httpPort = 0;
    bool haveAddress = false;
    for (int i = 0; i < argc; ++i)
    {
        if ((!Bstrcmp(argv[i], "--name") || !Bstrcmp(argv[i], "-name")) && i + 1 < argc)
            name = argv[++i];
        else if (!Bstrcmp(argv[i], "--mcp-http") && i + 1 < argc)
        {
            char *end;
            long const port = strtol(argv[++i], &end, 10);
            if (*end || port < 1 || port > UINT16_MAX)
                return EXIT_FAILURE;
            httpPort = port;
        }
        else if (!haveAddress && argv[i][0] != '-')
        {
            address = argv[i];
            haveAddress = true;
        }
        else
        {
            fprintf(stderr, "Usage: --editor-mcp [host[:port]] [--name name] [--mcp-http port]\n");
            return EXIT_FAILURE;
        }
    }
    Mcp mcp;
    mcp.address = address;
    mcp.name = name;
    mcp.session.join(address, name, 0, true);
    return httpPort ? httpServer(mcp, httpPort) : stdioServer(mcp);
}

bool Mcp_ParseOptions(int &argc, const char *const *&argv)
{
    bool stdio = false;
    int port = 0;
    gameArgs.clear();
    gameArgs.push_back(argv[0]);
    for (int i = 1; i < argc; ++i)
    {
        if (!Bstrcmp(argv[i], "--mcp-stdio"))
            stdio = true;
        else if (!Bstrcmp(argv[i], "--mcp-http"))
        {
            char *end;
            long const value = i + 1 < argc ? strtol(argv[++i], &end, 10) : 0;
            if (value < 1 || value > UINT16_MAX || *end)
            {
                fprintf(stderr, "--mcp-http requires a TCP port from 1 to 65535.\n");
                return false;
            }
            port = value;
        }
        else
            gameArgs.push_back(argv[i]);
    }
    if (!stdio && !port)
        return true;
    gameTransport.reset(new GameTransport);
    gameTransport->control.embedded = true;
    gameTransport->httpPort = port;
    gameTransport->stdio = stdio;
    if (stdio && !gameTransport->reserveStdout())
    {
        fprintf(stderr, "Could not reserve stdout for MCP.\n");
        return false;
    }
    argc = gameArgs.size();
    gameArgs.push_back(nullptr);
    argv = gameArgs.data();
    return true;
}

bool Mcp_Start()
{
    if (!gameTransport || gameTransport->open)
        return true;
    if (gameTransport->httpPort && !gameTransport->http.start(gameTransport->httpPort))
    {
        fprintf(stderr, "Could not start the local MCP HTTP endpoint.\n");
        return false;
    }
    if (gameTransport->stdio)
        gameTransport->input = startStdioInput();
    loguru::add_callback("VibeBuild32 MCP", captureConsole, nullptr, LOG_GAME_MAX);
    gameTransport->open = true;
    return true;
}

void Mcp_Poll()
{
    if (!gameTransport || !gameTransport->open || gameTransport->polling)
        return;
    gameTransport->polling = true;
    gameTransport->http.poll(gameTransport->control);
    if (gameTransport->stdio)
        gameTransport->pollStdin();
    gameTransport->polling = false;
}

bool Mcp_UsesStdin()
{
    return gameTransport && gameTransport->stdio;
}

void Mcp_SetNativeEditor()
{
    if (gameTransport)
        gameTransport->control.native = true;
}
