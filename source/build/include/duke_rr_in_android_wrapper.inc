// Shared Android input translation-unit wrapper used by Duke3D and Redneck Rampage.
// The including target supplies the game-local headers through its normal include path.

#include "compat.h"
#include "sdl_inc.h"
#include "baselayer.h"
#include "keys.h"
#include "duke3d.h"
#include "common_game.h"
#include "osd.h"
#include "player.h"
#include "game.h"
#include "build.h"
#include "anim.h"
#include "player.h"

#include "keyboard.h"
#include "control.h"
#include "_control.h"

#include "menus.h"

#ifdef __cplusplus
extern "C" {
#endif
extern int SDL_SendKeyboardKey(Uint8 state, SDL_Scancode scancode);
extern int SDL_SendKeyboardText(const char *text);
extern int SDL_SendMouseMotion(SDL_Window * window, Uint32 mouseID, int relative, int x, int y);
extern int SDL_SendMouseButton(SDL_Window * window, Uint32 mouseID, Uint8 state, Uint8 button);

#ifdef __cplusplus
}
#endif

#include "in_android.h"
#include "in_android_shared.inc"
