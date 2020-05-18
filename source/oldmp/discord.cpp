#include "discord.h"
#include "duke3d.h"

#define APPLICATION_ID "378644170576887820"
#define STEAM_APP_ID ""

int8_t discord_status;
DiscordRichPresence discordPresence;

int32_t last_max_kills;
int32_t last_kill_count;
int32_t last_frag_count;
int32_t last_secret_count;
int32_t last_max_secrets;
int32_t last_mode = -1;
int64_t time_started;

static void handleDiscordReady()
{
    initprintf("Discord: Ready!\n");
    discord_status = DISCORD_READY;
}

 static void handleDiscordDisconnected(int errcode, const char* message)
{
    initprintf("Discord: disconnected (%d: %s)\n", errcode, message);
    discord_status = DISCORD_DISCONNECTED;
}
    
static void handleDiscordError(int errcode, const char* message)
{
    initprintf("Discord: error (%d: %s)\n", errcode, message);
    discord_status = DISCORD_ERROR;
}
    
static void handleDiscordJoin(const char* secret)
{
    initprintf("Discord: join (%s)\n", secret);
}

static void handleDiscordSpectate(const char* secret)
{
    initprintf("Discord: spectate (%s)\n", secret);
}

static void handleDiscordJoinRequest(const DiscordUser* request)
{
    UNREFERENCED_PARAMETER(request);
}

void InitDiscord()
{
    initprintf("Initializing Discord Rich Presence...\n");
    DiscordEventHandlers handlers;
    memset(&handlers, 0, sizeof(handlers));
    handlers.ready = handleDiscordReady;
    handlers.errored = handleDiscordError;
    handlers.disconnected = handleDiscordDisconnected;
    handlers.joinGame = handleDiscordJoin;
    handlers.spectateGame = handleDiscordSpectate;
    handlers.joinRequest = handleDiscordJoinRequest;
    Discord_Initialize(APPLICATION_ID, &handlers, 1, STEAM_APP_ID);
}

void CheckDiscord()
{
    DukePlayer_t *ps = g_player[myconnectindex].ps;

    Discord_RunCallbacks();

    if (ps == NULL)
        return;

    if (last_mode != ps->gm)
    {
        if (!(last_mode & MODE_GAME) && (ps->gm & MODE_GAME))
        {
            time_started = (int64_t)time(NULL);
            UpdateDiscord();
        }
        else if((ps->gm & MODE_EOL) || ((last_mode & MODE_GAME) && !(ps->gm & MODE_GAME)))
        {
            time_started = 0;
            UpdateDiscord();
        }
    }

    if (ps->gm & MODE_GAME)
    {
        if (ud.multimode <= 1 || GTFLAGS(GAMETYPE_COOP))
        {
            if ((last_kill_count != ps->actors_killed) || (last_max_kills != ps->max_actors_killed))
            {
                UpdateDiscord();
            }
            else if ((last_secret_count != ps->secret_rooms) || (last_max_secrets != ps->max_secret_rooms))
            {
                UpdateDiscord();
            }
        }
        else
        {
            if (last_frag_count != (ps->frag - ps->fraggedself))
            {
                UpdateDiscord();
            }
        }
    }
}

// JM: Didn't know what else to call this.
// This turns all characters in a string to lower-case, then it capitalizes the
// first letter of each word, and terminates the string if it finds a left parenthesis.
// Used to make the gametype display look good, and prevent it from wrapping in Discord.
static void squeegee(char *src, char *s)
{
    int start = 1;

    Bstrcpy(s, src);

    for (; *s; s++)
    {
        *s = tolower(*s);
        if (start)
            *s = toupper(*s);
        
        if (*s == '(')
        {
            *s = '\0';
        }

        start = isspace(*s);
    }
}

static char *get_base_name(char *name)
{
    char *div, *basename;

    if ((div = strrchr(name, '/'))) {
        basename = strdup(div + 1);
    }
    else {
        basename = strdup(name);
    }

    return basename;
}

void UpdateDiscord()
{
    char buffer[3][256];
    char gametypename[128];

    DukePlayer_t *ps = g_player[myconnectindex].ps;

    memset(&discordPresence, 0, sizeof(discordPresence));

    if (ps == NULL)
        return;

    last_kill_count = ps->actors_killed;
    last_max_kills = ps->max_actors_killed;
    last_secret_count = ps->secret_rooms;
    last_max_secrets = ps->max_secret_rooms;
    last_frag_count = ps->frag - ps->fraggedself;
    last_mode = ps->gm;

    if (last_mode & MODE_GAME)
    {
        if (ud.multimode <= 1)
        {
            if (ud.player_skill >= 1 && ud.player_skill <= 4)
            {
                //Bstrcpy(skillname, SkillNames[ud.player_skill - 1]);
                //capitalize(skillname);
                Bsprintf(buffer[0], u8"SP | \U0001F480 %d/%d \U0001F50D %d/%d", last_kill_count, last_max_kills, last_secret_count, last_max_secrets);
            }
            else
            {
                Bsprintf(buffer[0], "Singleplayer (No Monsters)");
            }
        }
        else
        {
            squeegee(GametypeNames[ud.coop], gametypename);

            if (!GTFLAGS(GAMETYPE_COOP))
            {
                Bsprintf(buffer[0], u8"%s | \U0001F480 %d", gametypename, last_frag_count);
            }
            else
            {
                Bsprintf(buffer[0], u8"%s | \U0001F480 %d \U0001F50D %d/%d", gametypename, last_kill_count, last_secret_count, last_max_secrets);
            }
        }

        if (MapInfo[(ud.volume_number*MAXLEVELS) + ud.level_number].name != NULL)
        {
            if (currentboardfilename[0] != 0 && ud.volume_number == 0 && ud.level_number == 7)
            {
                Bsprintf(buffer[1], "%s", get_base_name(currentboardfilename));
            }
            else
            {
                Bsprintf(buffer[1], "%s", MapInfo[(ud.volume_number*MAXLEVELS) + ud.level_number].name);
            }
        }
    }
    else if (last_mode & MODE_EOL)
    {
        Bsprintf(buffer[0], "In Intermission");
        memset(buffer[1], 0, sizeof(buffer[1]));
    }
    else
    {
        memset(buffer[0], 0, sizeof(buffer[1]));
        memset(buffer[1], 0, sizeof(buffer[1]));
    }

    discordPresence.state = buffer[0];
    discordPresence.details = buffer[1];
    discordPresence.startTimestamp = time_started;
    discordPresence.largeImageKey = "default";
    discordPresence.largeImageText = buffer[1];
    discordPresence.smallImageKey = "default_small";
    discordPresence.smallImageText = HEAD2;

    Discord_UpdatePresence(&discordPresence);
}