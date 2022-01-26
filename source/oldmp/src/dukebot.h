#ifndef DUKEBOT_H
#define DUKEBOT_H

#include "build.h"
#include "player.h"

typedef struct
{
    vec3_t goalPos;
    int32_t moveCount;
    int32_t goalSprite, goalSpriteScore, goalPlayer, goalWall;
    int16_t goalSect;
} DukeBotAI_t;

void DukeBot_GetInput(int snum, input_t* syn);

#endif // DUKEBOT_H