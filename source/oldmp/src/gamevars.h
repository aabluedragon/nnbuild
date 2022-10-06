#ifndef GAMEVARS_H
#define GAMEVARS_H

#include "gamedef.h"
#include "vfs.h"

int __fastcall Gv_GetVar(int id, int iActor, int iPlayer);
void __fastcall Gv_SetVar(int id, int lValue, int iActor, int iPlayer);
int __fastcall Gv_GetVarX(int id);
void __fastcall Gv_SetVarX(int id, int lValue);

void Gv_NewVar(const char* pszLabel, intptr_t lValue, uint32_t dwFlags);
int Gv_NewArray(const char* pszLabel, int asize);

#endif // GAMEVARS_H