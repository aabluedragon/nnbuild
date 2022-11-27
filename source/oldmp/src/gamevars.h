#ifndef GAMEVARS_H
#define GAMEVARS_H

#include "gamedef.h"
#include "vfs.h"

#define GV_FLAG_CONSTANT	(MAXGAMEVARS)
#define GV_FLAG_NEGATIVE	(MAXGAMEVARS<<1)
#define GV_FLAG_ARRAY		(MAXGAMEVARS<<2)
#define GV_FLAG_STRUCT		(MAXGAMEVARS<<3)

int __fastcall Gv_GetVar(int id, int iActor, int iPlayer);
void __fastcall Gv_SetVar(int id, int lValue, int iActor, int iPlayer);
int __fastcall Gv_GetVarX(int id);
void __fastcall Gv_SetVarX(int id, int lValue);

void Gv_NewVar(const char* pszLabel, intptr_t lValue, uint32_t dwFlags);
int Gv_NewArray(const char* pszLabel, intptr_t asize);

int Gv_GetVarByLabel(const char* szGameLabel, int lDefault, int iActor, int iPlayer);
void Gv_DumpValues(void);
void Gv_ResetSystemDefaults(void);
void Gv_InitWeaponPointers(void);
void Gv_RefreshPointers(void);
void Gv_Init(void);
void Gv_WriteSave(FILE* fil);
int Gv_ReadSave(int fil);

#endif // GAMEVARS_H