#ifndef DUKEBOT_H
#define DUKEBOT_H

#include "build.h"
#include "player.h"

#define BOT_MAXSPEED 2560
#define BOT_SEEK_GIVEUP 150
#define BOT_SEEK_LOOKFORBETTER 30

enum
{
    BOT_MOVE_STRAFE     = (1 << 0),
    BOT_MOVE_JUMP       = (1 << 1),
    BOT_MOVE_CROUCH     = (1 << 2),
    BOT_MOVE_USE        = (1 << 3),
    BOT_MOVE_ASCEND     = (1 << 4),
    BOT_MOVE_DESCEND    = (1 << 5),
};

enum
{
    BOT_COMBAT_MELEE    = (1 << 0),
    BOT_COMBAT_STOMP    = (1 << 1),
};

typedef struct
{
    vec3_t goalPos, lastSeenPlayerPos[MAXPLAYERS];
    int32_t moveCount, cycleCount, seekSpriteFailCount, seekSpriteCooldown;
    int32_t goalSprite, goalSpriteScore, lastGoalSprite;
    int32_t goalPlayer, goalWall;
    int16_t lastStartSect, lastEndSect;
    uint8_t active;
} DukeBotAI_t;

void DukeBot_ResetAll();
void DukeBot_Activate(int32_t playerNum);
void DukeBot_GetInput(int snum, input_t* syn);

#endif // DUKEBOT_H