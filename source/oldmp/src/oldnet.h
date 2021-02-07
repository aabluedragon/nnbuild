#ifndef OLDNET_H
#define OLDNET_H

#include "duke3d.h"
#include "sync.h"

extern int quittimer;
extern int lastpackettime;
extern int mymaxlag, otherminlag, bufferjitter;
extern int voting, vote_map, vote_episode;

extern int movefifosendplc;
extern int movefifoplc;

extern int predictfifoplc;

extern int oldnet_predicting;
extern DukePlayer_t predictedPlayer;
extern vec3_t myposbak[MOVEFIFOSIZ];
extern fix16_t myhorizbak[MOVEFIFOSIZ], myangbak[MOVEFIFOSIZ];

enum DukePacket_t
{
    PACKET_TYPE_MASTER_TO_SLAVE,
    PACKET_TYPE_SLAVE_TO_MASTER,
    PACKET_TYPE_BROADCAST,
    SERVER_GENERATED_BROADCAST,
    PACKET_TYPE_VERSION,

    /* don't change anything above this line */

    PACKET_TYPE_MESSAGE,

    PACKET_TYPE_NEW_GAME,
    PACKET_TYPE_RTS,
    PACKET_TYPE_MENU_LEVEL_QUIT,
    PACKET_TYPE_WEAPON_CHOICE,
    PACKET_TYPE_PLAYER_OPTIONS,
    PACKET_TYPE_PLAYER_NAME,
    PACKET_TYPE_INIT_SETTINGS,

    PACKET_TYPE_USER_MAP,

    PACKET_TYPE_MAP_VOTE,
    PACKET_TYPE_MAP_VOTE_INITIATE,
    PACKET_TYPE_MAP_VOTE_CANCEL,

    PACKET_TYPE_LOAD_GAME,
    PACKET_TYPE_NULL_PACKET,
    PACKET_TYPE_PLAYER_READY,
    PACKET_TYPE_FRAGLIMIT_CHANGED,
    PACKET_TYPE_EOL,
    PACKET_TYPE_PING,
    PACKET_TYPE_QUIT = 255 // should match mmulti I think
};

enum NetMode_t
{
    NETMODE_MASTERSLAVE,
    NETMODE_P2P,
    NETMODE_OFFLINE = 255
};

void faketimerhandler(void);
void Net_HandleInput(void);
void Net_GetPackets(void);
void Net_ParsePackets(void);
void Net_SendQuit(void);
void Net_SendWeaponChoice(void);
void Net_SendVersion(void);
void Net_SendPlayerOptions(void);
void Net_SendFragLimit(void);
void Net_SendPlayerName(void);
void Net_SendUserMapName(void);
void Net_SendInitialSettings(void);
void Net_NewGame(int volume, int level);
void Net_EndOfLevel(void);
void Net_EnterMessage(void);
void Net_CorrectPrediction(void);
void Net_ResetPrediction(void);
void Net_DoPrediction(void);
void Net_ClearFIFO(void);
void waitforeverybody(void);
void allowtimetocorrecterrorswhenquitting(void);

#endif
