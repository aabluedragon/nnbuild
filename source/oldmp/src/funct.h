//-------------------------------------------------------------------------
/*
Copyright (C) 1996, 2003 - 3D Realms Entertainment
Copyright (C) 2000, 2003 - Matt Saettler (EDuke Enhancements)
Copyright (C) 2004, 2007 - EDuke32 developers

This file is part of EDuke32

EDuke32 is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License version 2
as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/
//-------------------------------------------------------------------------

#ifndef __funct_h__
#define __funct_h__

#ifndef _MSC_VER
  #ifdef __GNUC__
    #ifndef __fastcall
      #define __fastcall __attribute__((fastcall))
    #endif
  #else
    #define __fastcall
  #endif
#endif

extern void sendscore(const char *s);
extern int A_CallSound(int sn,int whatsprite);
extern int check_activator_motion(int lotag);
extern int CheckDoorTile(int dapic);
extern int isanunderoperator(int lotag);
extern int isanearoperator(int lotag);
extern int CheckPlayerInSector(int sect);
extern int __fastcall A_FindPlayer(spritetype *s,int *d);
extern int P_FindOtherPlayer(int p,int *d);

extern void G_DoSectorAnimations(void);
extern int32_t* G_GetAnimationPointer(int32_t index);
extern int GetAnimationGoal(int16_t target, int8_t type);
extern int SetAnimation(int16_t target, int8_t type, int thegoal, int thevel);

extern void G_AnimateCamSprite(void);
extern void G_AnimateWalls(void);
extern int G_ActivateWarpElevators(int s,int d);
extern void G_OperateSectors(int sn,int ii);
extern void G_OperateRespawns(int low);
extern void G_OperateActivators(int lotag, int playerNum);
extern void G_OperateMasterSwitches(int low);
extern void G_OperateForceFields(int s,int low);
extern int P_ActivateSwitch(int snum,int w,int switchtype);
extern void activatebysector(int sect,int j);
extern void P_CheckTouchDamage(DukePlayer_t *p,int j);

// [JM] I should make a sector.h
extern void A_DamageWall(int spriteNum, int wallNum, const vec3_t& vPos, int weaponNum);
extern void A_DamageWall_Internal(int spriteNum, int wallNum, const vec3_t& vPos, int weaponNum);
extern void Sect_DamageFloor(int const spriteNum, int const sectNum);
extern void Sect_DamageFloor_Internal(int const spriteNum, int const sectNum);
extern void Sect_DamageCeiling(int const spriteNum, int const sectNum);
extern void Sect_DamageCeiling_Internal(int const spriteNum, int const sectNum);


extern void A_DamageObject_Internal(int i,int sn);
extern void A_DamageObject(int i, int sn);
extern void allignwarpelevators(void);
extern void G_HandleSharedKeys(int snum);
extern void checksectors(int snum);
extern void G_CacheMapData(void);
extern void xyzmirror(int i,int wn);
extern void G_UpdateScreenArea(void);
extern void P_RandomSpawnPoint(int snum);
extern void P_ResetWeapons(int snum);
extern void P_ResetInventory(int snum);
extern void G_NewGame(int vn,int ln,int sk);
extern void G_ResetTimers(void);
extern void clearfifo(void);
extern int  G_EnterLevel(int g);
extern void G_BackToMenu(void);
extern void P_UpdateScreenPal(DukePlayer_t *p);
extern void P_QuickKill(DukePlayer_t *p);
extern int A_GetHitscanRange(int i);
extern int32_t A_CheckHitSprite(int spriteNum, int16_t* hitSprite);
extern int A_Shoot(int i,int atwith);
extern void P_DisplayScubaMask(int snum);
extern void P_DisplayWeapon(int snum);
extern void P_GetInput(int snum, bool uncapped);
extern void P_DropWeapon(DukePlayer_t *p);
extern void P_ProcessInput(int snum);
extern void ChangeToMenu(int cm);
// extern void savetemp(const char *fn,int daptr,int dasiz);
// extern int G_LoadSaveHeader(char spot,int32_t *vn,int32_t *ln,int32_t *psk,int32_t *numplr);
extern int G_LoadPlayer(int spot);
extern int G_SavePlayer(int spot);
extern int menutext_(int x,int y,int s,int p,const char *t,int bits);
#define menutext(x,y,s,p,t) menutext_(x,y,s,p,OSD_StripColors(menutextbuf,t),10+16)
extern void M_DisplayMenus(void);
extern void G_PlayAnim(const char *fn,char);
extern int G_GetAngleDelta(int a,int na);
extern void A_GetZLimits(int iActor);
extern void A_Fall(int iActor);
extern void C_Compile(const char *fn);
extern int A_GetFurthestAngle(int const spriteNum, int const angDiv);
extern void A_Execute(int iActor,int iPlayer,int lDist);
extern void overwritesprite(int thex,int they,int tilenum,int shade,int stat,int dapalnum);
extern void gamenumber(int x,int y,int n,int s);
extern void G_Shutdown(void);
extern void A_PlayAlertSound(int i);
extern int A_CheckInventorySprite(spritetype *s);
extern int A_CheckEnemySprite(void const* s);
extern int A_CheckEnemyTile(int pn);
extern void G_DrawTile(int x,int y,int tilenum,int shade,int orientation);
extern void G_DrawTilePal(int x,int y,int tilenum,int shade,int orientation,int p);
extern void G_DrawFrags(void);
extern void P_DoQuote(int q,DukePlayer_t *p);
extern void G_GameExit(const char *t);
extern void G_DisplayRest(int smoothratio);
extern void updatesectorz(int x,int y,int z,short *sectnum);
extern void G_DrawBackground(void);
extern void G_DrawRooms(int snum,int smoothratio);
extern int A_InsertSprite(int whatsect,int s_x,int s_y,int s_z,int s_pn,int s_s,int s_xr,int s_yr,int s_a,int s_ve,int s_zv,int s_ow,int s_ss);
extern int A_CheckSwitchTile(int i);
extern int A_Spawn(int j,int pn);
extern void G_DoSpriteAnimations(int32_t x, int32_t y, int32_t z, int32_t a, int32_t smoothratio);
extern int main(int argc,char **argv);
extern void G_OpenDemoWrite(void);
extern void G_CloseDemoWrite(void);
extern void A_SpawnWallGlass(int i,int wallnum,int n);
extern void A_SpawnGlass(int i,int n);
extern void A_SpawnCeilingGlass(int i,int sectnum,int n);
extern void A_SpawnRandomGlass(int i,int wallnum,int n);
extern int GetTime(void);
extern void CONFIG_SetDefaults(void);
extern void CONFIG_ReadKeys(void);
extern void ReadSaveGameHeaders(void);
extern void G_UpdateInterpolations(void);
extern void G_SetInterpolation(int *posptr);
extern void G_StopInterpolation(int *posptr);
extern void G_DoInterpolations(int smoothratio);
extern void G_RestoreInterpolations(void);
extern int G_CheckForSpaceCeiling(int sectnum);
extern int G_CheckForSpaceFloor(int sectnum);
extern void P_AddAmmo(int weapon,DukePlayer_t *p,int amount);
extern void P_AddWeaponNoSwitch(DukePlayer_t *p,int weapon);
extern void P_AddWeapon(DukePlayer_t* pPlayer, int weaponNum, int switchWeapon);
extern void P_SelectNextInvItem(DukePlayer_t *p);
extern void P_CheckWeapon(DukePlayer_t *p);
extern void A_RadiusDamage(int i,int r,int hp1,int hp2,int hp3,int hp4);
extern int A_MoveSprite(int spritenum,int xchange,int ychange,int zchange,unsigned int cliptype);
extern int A_SetSprite(int i,unsigned int cliptype);
extern void A_AddToDeleteQueue(int i);
extern void A_SpawnMultiple(int sp,int pic,int n);
extern void A_DoGuts(int sp,int gtype,int n);
extern void Sect_SetInterpolation(int i);
extern void Sect_ClearInterpolation(int i);
extern int A_IncurDamage(int sn);
extern void G_MoveWorld(void);
extern void A_MoveCyclers(void);
extern void A_MoveDummyPlayers(void);

// premap.c
extern void G_SetupGlobalPsky(void);

// player.c
extern void playerColor_init(void);
extern void playerColor_add(const char *name, int palnum);
extern void playerColor_delete(int palnum);
extern uint8_t playerColor_getNextColor(uint8_t palnum, uint8_t useauto);
extern uint8_t playerColor_getPreviousColor(uint8_t palnum, uint8_t useauto);

extern void P_SetWeaponGamevars(int playerNum, const DukePlayer_t* const pPlayer);
extern void P_UpdatePosWhenViewingCam(DukePlayer_t* pPlayer);
extern int Proj_GetDamage(projectile_t* pProj);

// game.cpp
extern int mpgametext(int y, const char *t, int s, int dabits);
extern void G_SetStatusBarScale(int sc);

extern void G_HandleMirror(int32_t x, int32_t y, int32_t z, fix16_t a, fix16_t q16horiz, int32_t smoothratio);
extern int G_GameTextLen(int x, const char *t);
extern int G_PrintGameText(int small, int starttile, int x,int y,const char *t,int s,int p,int orientation,int x1, int y1, int x2, int y2,int z);
extern void G_DrawTXDigiNumZ(int starttile, int x,int y,int n,int s,int pal,int cs,int x1, int y1, int x2, int y2, int z);
extern void G_DrawTileSmall(int x,int y,int tilenum,int shade,int orientation);
extern void G_DrawTilePalSmall(int x,int y,int tilenum,int shade,int orientation,int p);
extern void Gv_ResetVars(void);
extern void A_ResetVars(int iActor);
extern void G_AddKillToFeed(int victim, int attacker);

extern int minitext_(int x,int y,const char *t,int s,int p,int sb);

extern int32_t G_GetStringLineLength(const char *text, const char *end, const int32_t iter);
extern int32_t G_GetStringNumLines(const char *text, const char *end, const int32_t iter);
extern char* G_GetSubString(const char *text, const char *end, const int32_t iter, const int32_t length);
extern int32_t G_GetStringTile(int32_t font, char *t, int32_t f);

extern vec2_t G_ScreenTextSize(const int32_t font, int32_t x, int32_t y, const int32_t z, const int32_t blockangle, const char *str, const int32_t o, int32_t xspace, int32_t yline, int32_t xbetween, int32_t ybetween, const int32_t f, int32_t x1, int32_t y1, int32_t x2, int32_t y2);

extern void G_AddCoordsFromRotation(vec2_t *coords, const vec2_t *unitDirection, const int32_t magnitude);
extern vec2_t G_ScreenText(const int32_t font, int32_t x, int32_t y, const int32_t z, const int32_t blockangle, const int32_t charangle, const char *str, const int32_t shade, int32_t pal, int32_t o, const int32_t alpha, int32_t xspace, int32_t yline, int32_t xbetween, int32_t ybetween, const int32_t f, const int32_t x1, const int32_t y1, const int32_t x2, const int32_t y2);

#define minitextshade(x, y, t, s, p, sb) minitext_(x,y,t,s,p,sb)
#define minitext(x, y, t, p, sb) minitext_(x,y,t,0,p,sb)

#define gametext(x,y,t,s,dabits) G_PrintGameText(0,STARTALPHANUM, x,y,t,s,0,dabits,0, 0, xdim-1, ydim-1, 65536)
#define gametextscaled(x,y,t,s,dabits) G_PrintGameText(1,STARTALPHANUM, x,y,t,s,0,dabits,0, 0, xdim-1, ydim-1, 65536)
#define gametextpal(x,y,t,s,p) G_PrintGameText(0,STARTALPHANUM, x,y,t,s,p,26,0, 0, xdim-1, ydim-1, 65536)
#define gametextpalbits(x,y,t,s,p,dabits) G_PrintGameText(0,STARTALPHANUM, x,y,t,s,p,dabits,0, 0, xdim-1, ydim-1, 65536)

extern void G_InitDynamicTiles();
extern void G_ProcessDynamicTileMapping(const char *szLabel, int lValue);

extern void A_LoadActor(int sActor);

extern int Gv_GetVarByLabel(const char *szGameLabel, int lDefault, int iActor, int iPlayer);
extern void Gv_DumpValues(void);
// extern void AddLog(const char *psz, ...);

extern void Gv_ResetSystemDefaults(void);
extern void Gv_InitWeaponPointers(void);
extern void Gv_RefreshPointers(void);
extern void Gv_Init(void);
extern void Gv_WriteSave(FILE *fil);
extern int Gv_ReadSave(int fil);

extern int __fastcall Gv_GetVar(int id, int iActor, int iPlayer);
extern void __fastcall Gv_SetVar(int id, int lValue, int iActor, int iPlayer);
extern int __fastcall Gv_GetVarX(int id);
extern void __fastcall Gv_SetVarX(int id, int lValue);

// extern void SetGameArrayID(int id,int index, int lValue);

extern int Gv_NewVar(const char *pszLabel, int lValue, unsigned int dwFlags);
extern int Gv_NewArray(const char *pszLabel, int asize);
extern void C_ReportError(int iError);

extern void onvideomodechange(int newmode);

extern void X_OnEvent(int iEventID, int sActor, int sPlayer, int lDist);
extern int X_OnEventWithReturn(int iEventID, int sActor, int sPlayer, int nReturn);

extern void G_UpdatePlayerFromMenu(void);

extern void G_AddUserQuote(const char *daquote);

extern int A_CheckSpriteFlags(int iActor, int iType);
extern int A_CheckSpriteTileFlags(int iPicnum, int iType);

extern void se40code(int x,int y,int z,int a,int h, int smoothratio);

extern void G_FreeMapState(int mapnum);
extern int32_t G_FindLevelByFile(const char *fn);

extern void G_ClearCameraView(DukePlayer_t* ps);

extern int _EnterText(int small,int x,int y,char *t,int dalen,int c);

#define G_EnterText(x, y, t, dalen, c) _EnterText(0,x,y,t,dalen,c)
#define Net_EnterText(x, y, t, dalen, c) _EnterText(1,x,y,t,dalen,c)

#endif // __funct_h__
