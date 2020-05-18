#ifndef DISCORD_H
#define DISCORD_H
#include "discord_rpc.h"

extern int8_t discord_status;
extern DiscordRichPresence discordPresence;

#define DISCORD_WAITING 0
#define DISCORD_READY 1
#define DISCORD_DISCONNECTED 2
#define DISCORD_ERROR 3

void InitDiscord();
void CheckDiscord();
void UpdateDiscord();

#endif // DISCORD_H