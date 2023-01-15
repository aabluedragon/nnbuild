#ifndef CHATPIPE_H
#define CHATPIPE_H

#include "build.h"

#define CHATPIPE_BUFSIZE 4096
#define CHATPIPE_TIMEOUT 5000

void ChatPipe_Create(void);
void ChatPipe_SendMessage(const char* message);
void ChatPipe_Poll(void);

#endif // CHATPIPE_H