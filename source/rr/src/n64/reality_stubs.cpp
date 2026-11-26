// Duke Nukem 64 ("Reality") link stubs for the NNBuild unified build.
//
// Rednukem bundles a Duke Nukem 64 total conversion ("Reality", the n64/ dir)
// alongside Redneck Rampage. Its renderer (reality_render.cpp) needs a large set
// of DN64-specific engine patches (DAMETH_N64* texture-cache methods, N64 GLSL
// shaders, a custom tile decompressor) that our modernized Build engine does not
// carry, so the Reality module is not compiled here.
//
// The Redneck Rampage game code still *references* the DN64 entry points, but only
// behind `if (REALITY)` (REALITY == g_gameType & GAMEFLAG_REALITY), which is never
// set for Redneck Rampage. These stubs satisfy the linker for that dead code; none
// of them is ever reached at runtime when playing Redneck Rampage. If DN64 support
// is ever wanted, drop this file and build the real n64/reality*.cpp instead (plus
// the engine-side DAMETH_N64/PTH_N64/polymost_setuptexture patches upstream added).

#include "build.h"
#include "reality.h"
#include "../duke3d.h"

// --- globals the RR game code (and reality.h dataspecs) reference ------------
buildvfs_kfd rt_group = buildvfs_kfd_invalid;
uint16_t rt_palette[RT_PALNUM][256];
int rt_vtxnum;
rt_vertex_t *rt_sectvtx;
rt_walltype *rt_wall;
rt_sectortype *rt_sector;
int rt_boardnum;
int rt_levelnum;
vec3f_t rt_sky_color[2];
int16_t rt_soundrate[MAXSOUNDS];

int ms_list[MOVESECTNUM], ms_listvtxptr[MOVESECTNUM];
int ms_list_cnt, ms_vtx_cnt;
int ms_dx[MOVESECTVTXNUM], ms_dy[MOVESECTVTXNUM];
int ms_vx[MOVESECTVTXNUM], ms_vy[MOVESECTVTXNUM];
explosioninstance_t explosions[MAXEXPLOSIONS];
smokeinstance_t smoke[MAXEXPLOSIONS];

int boss2seq, boss2seqframe;
int boss2mdlstate, boss2mdlstate2;
int boss2timer_step;
int boss2_frame, boss2_frame2;
int boss2timer;
float boss2_interp;

// --- entry points, all no-ops (dead code unless REALITY, never true for RR) ---
buildvfs_kfd RT_InitGRP(const char *) { return buildvfs_kfd_invalid; }
void RT_Init(void) {}
void RT_InitSound(void) {}
void RT_Intro(void) {}
void RT_Bonus(void) {}
int  RT_PrepareScript(void) { return 0; }
void RT_LoadBoard(int) {}
int  RT_NextLevel(void) { return 0; }
void RT_LoadTiles(void) {}
void RT_LoadBOSS2MDL(void) {}
int  RT_LoadSound(int) { return 0; }
int  RT_KRand2(void) { return 0; }
float RT_GetAngle(float, float) { return 0.f; }

void RT_DrawRooms(int, int, int, fix16_t, fix16_t, int16_t, int) {}
int  RT_DrawStatusBar(int) { return 0; }
void RT_EnablePolymost() {}
void RT_DisablePolymost(int) {}
void RT_AnimateModels(void) {}
void RT_ResetBarScroll(void) {}
void RT_RotateSprite(float, float, float, float, int, int, bool) {}
void RT_RotateSpriteText(float, float, float, float, int, int, bool) {}
void RT_RotateSpriteSetColor(int, int, int, int) {}
void RT_RotateSpriteSetShadePalAlpha(int, int, int) {}

void RT_AddExplosion(int16_t, int16_t, int16_t, uint8_t) {}
void RT_AddSmoke(int16_t, int16_t, int16_t, uint8_t) {}
void RT_AdjustCeilingPanning(int, int, int) {}
void RT_AdjustFloorPanning(int, int, int) {}
void RT_MS_Add(int, int, int) {}
void RT_MS_SetInterpolation(int) {}
void RT_MS_Update(int, int, int, int) {}

void RT_P_DisplayScuba(void) {}
void RT_P_DisplayWeapon(void) {}
void RT_P_ProcessWeapon(int) {}

rt_soundinstance_t *RT_FindSoundSlot(int, int) { return nullptr; }
void RT_FreeSoundSlot(rt_soundinstance_t *) {}
void RT_FreeSoundSlotId(int) {}
void RT_SoundDecodeEnv(const char **, uint32_t *, void *) {}

void RT_PlaySong(void) {}
void RT_StopSong(void) {}
void RT_PauseSong(bool) {}
