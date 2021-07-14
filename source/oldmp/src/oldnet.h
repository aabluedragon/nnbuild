#ifndef OLDNET_H
#define OLDNET_H

#include "duke3d.h"
#include "sync.h"

#ifdef OLDNET_CPP_
#define OLDNET_EXTERN
#else
#define OLDNET_EXTERN extern
#endif

enum
{
    PREDICTSTATE_OFF = 0,
    PREDICTSTATE_PROCESS = 1,
    PREDICTSTATE_CORRECT = 2,
};

#define PREDICT_CURPOS (predictfifoplc & (MOVEFIFOSIZ - 1))
#define PREDICT_LASTPOS ((movefifoplc - 1) & (MOVEFIFOSIZ - 1))

#define CREATE_PREDICTED_LINKED_LIST(name) decltype(head##name) predicted_head##name; \
                                           decltype(prev##name) predicted_prev##name; \
                                           decltype(next##name) predicted_next##name

#define swap_predicted_linked_list(name) std::swap(head##name, predicted_head##name), \
                                         std::swap(prev##name, predicted_prev##name), \
                                         std::swap(next##name, predicted_next##name)

#define reset_predicted_linked_list(name) memcpy(predicted_head##name, head##name, sizeof(predicted_head##name)), \
                                          memcpy(predicted_prev##name, prev##name, sizeof(predicted_prev##name)), \
                                          memcpy(predicted_next##name, next##name, sizeof(predicted_next##name))

#if 0
typedef struct
{
    //vec3_t pos;
    //fix16_t q16horiz, q16ang;
    char playerHash;
} PredictBackup_t;
#endif

extern int quittimer;
extern int lastpackettime;
extern int mymaxlag, otherminlag, bufferjitter;
extern int voting, vote_map, vote_episode;

extern int movefifosendplc;
extern int movefifoplc;

OLDNET_EXTERN input_t netInput;

// Check if oldnet_predicting is true and return in code branches
// you want to avoid going into during prediction.
OLDNET_EXTERN int32_t oldnet_predicting;

// Must enable this on code branches that are able to predict sounds
OLDNET_EXTERN bool sounds_predictable;

OLDNET_EXTERN int predictfifoplc;

OLDNET_EXTERN DukePlayer_t* originalPlayer;
OLDNET_EXTERN DukePlayer_t predictedPlayer;
OLDNET_EXTERN spritetype predicted_pSprite;
OLDNET_EXTERN ActorData_t predicted_pActor;

OLDNET_EXTERN intptr_t* original_plValues[MAXGAMEVARS];
OLDNET_EXTERN intptr_t* predicted_plValues[MAXGAMEVARS];

//OLDNET_EXTERN PredictBackup_t predictBackup[MOVEFIFOSIZ];

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
void Net_InitializePrediction(void);
void Net_DoPrediction(int state);
void Net_UsePredictedPointers(void);
void Net_UseOriginalPointers(void);

void Net_ClearFIFO(void);
void Net_CheckPlayerQuit(int i);
void waitforeverybody(void);
void allowtimetocorrecterrorswhenquitting(void);

#endif
