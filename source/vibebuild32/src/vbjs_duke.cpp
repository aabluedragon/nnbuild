// VibeBuild32 JS scripting — Duke3D game bindings.
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

#if SDL_MAJOR_VERSION >= 2 && defined(USE_OPENGL)
# include "imgui.h"
#endif

extern hashtable_t h_gamevars;   // gamevars.cpp
extern hashtable_t h_labels;     // gamedef.cpp (CON defines/constants)

static int vbjs_playerSprite(void)
{
    if ((unsigned)myconnectindex >= MAXPLAYERS || !g_player[myconnectindex].ps)
        return -1;
    return g_player[myconnectindex].ps->i;
}

namespace vbjs_game {

void log(const char *s)
{
    LOG_F(INFO, "[js] %s", s ? s : "");
    OSD_Printf("[js] %s\n", s ? s : "");
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

    return A_InsertSprite(sect, x, y, z, tile, 0, 48, 48, 0, 0, 0, (owner >= 0 ? owner : psp), 1);
}

void sendChat(const char *text)
{
    if (!text || !text[0]) return;

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

void runCommand(const char *cmd)
{
    if (!cmd) return;
    while (*cmd == '/' || *cmd == ' ') cmd++;   // tolerate a leading console-style '/'
    if (*cmd) OSD_Dispatch(cmd);
}

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
    int const id = hash_find(&h_labels, name);
    if (id >= 0 && (labeltype[id] & LABEL_DEFINE)) { if (ok) *ok = 1; return labelcode[id]; }
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
    {
        static char expression[256] = "$event";
        static char result[1024] = "";
        static const char *events[] = { "start", "frame", "playerDied", "actorSpawned", "chatMessage", "shutdown" };

        engineBeginImGuiFrame();
        ImGui::SetNextWindowPos(ImVec2((float)xdim - 410.f, 8.f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(402.f, (float)ydim - 16.f), ImGuiCond_Always);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
        ImGui::Begin("VibeBuild32 JavaScript DevTools", nullptr, flags);

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
            if (ImGui::BeginTabItem("Breakpoints"))
            {
                ImGui::TextUnformatted("Event breakpoints pause after a listener runs.");
                ImGui::TextDisabled("Use vb.breakpoint() for an exact source-line pause.");
                for (auto event : events)
                {
                    bool enabled = VBJS_DevtoolsHasEventBreakpoint(event) != 0;
                    if (ImGui::Checkbox(event, &enabled)) VBJS_DevtoolsSetEventBreakpoint(event, enabled);
                }
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
