#ifndef DUKEBOT_H
#define DUKEBOT_H

#include "build.h"
#include "player.h"

#define BOT_MAXSPEED 2560
#define BOT_SEEK_GIVEUP 150
#define BOT_SEEK_LOOKFORBETTER 30

enum
{
    BOT_MOVETYPE_JUMP = (1 << 0),
    BOT_MOVETYPE_CROUCH = (1 << 1),
    BOT_MOVETYPE_USE = (1 << 2),
    BOT_MOVETYPE_ASCEND = (1 << 3),
    BOT_MOVETYPE_DESCEND = (1 << 4),
};

typedef struct
{
    vec3_t goalPos, lastSeenPlayerPos[MAXPLAYERS];
    int32_t moveCount, cycleCount, seekSpriteFailCount, seekSpriteCooldown;
    int32_t goalSprite, goalSpriteScore, lastGoalSprite;
    int32_t goalPlayer, goalWall;
    uint8_t active;
} DukeBotAI_t;

void DukeBot_ResetAll();
void DukeBot_Activate(int32_t playerNum);
void DukeBot_GetInput(int snum, input_t* syn);

#endif // DUKEBOT_H