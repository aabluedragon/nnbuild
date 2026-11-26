// Shared Duke/Redneck input translation-unit wrapper.

#if __has_include("global.h")
# include "global.h"
#else
# include "../../duke3d/src/global.h"
#endif

#if __has_include("game.h")
# include "game.h"
#else
# include "../../duke3d/src/game.h"
#endif

#if __has_include("function.h")
# include "function.h"
#else
# include "../../duke3d/src/function.h"
#endif

#include "keyboard.h"
#include "mouse.h"
#include "joystick.h"
#include "control.h"

#if __has_include("input.h")
# include "input.h"
#else
# include "../../duke3d/src/input.h"
#endif

#if __has_include("menus.h")
# include "menus.h"
#else
# include "../../duke3d/src/menus.h"
#endif

#include "duke_rr_input_shared.inc"
