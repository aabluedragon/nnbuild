// Shared setup/control constants used by multiple games.

#ifndef gamedefs_public_h_
#define gamedefs_public_h_

#ifndef GAMEDEFS_DEFINE_SETUPFILENAME
# define GAMEDEFS_DEFINE_SETUPFILENAME 1
#endif

#ifndef GAMEDEFS_DEFINE_MOUSE_ANALOGUE_SCALE
# define GAMEDEFS_DEFINE_MOUSE_ANALOGUE_SCALE 1
#endif

#define GAMEDEFS_JOYSTICK_DEFAULT_SCALE       1
#define GAMEDEFS_JOYSTICK_DEFAULT_SENSITIVITY 2

#ifndef GAMEDEFS_JOYSTICK_DEFAULT
# define GAMEDEFS_JOYSTICK_DEFAULT GAMEDEFS_JOYSTICK_DEFAULT_SCALE
#endif

#ifdef __cplusplus
extern "C" {
#endif

// config file name
#if GAMEDEFS_DEFINE_SETUPFILENAME && !defined(SETUPFILENAME)
# define SETUPFILENAME APPBASENAME ".cfg"
#endif

// KEEPINSYNC mact/include/_control.h, build/src/sdlayer.cpp
#define MAXJOYBUTTONS 32
#define MAXJOYBUTTONSANDHATS (MAXJOYBUTTONS+4)

// KEEPINSYNC mact/include/_control.h, build/src/sdlayer.cpp
#define MAXMOUSEAXES 2

// KEEPINSYNC mact/include/_control.h, build/src/sdlayer.cpp
#define MAXJOYAXES 9
#define MAXJOYDIGITAL (MAXJOYAXES*2)

// default mouse scale
#if GAMEDEFS_DEFINE_MOUSE_ANALOGUE_SCALE
# define DEFAULTMOUSEANALOGUESCALE           65536
#endif

// default joystick settings

#if defined(GEKKO)
# define DEFAULTJOYSTICKANALOGUESCALE        16384
# define DEFAULTJOYSTICKANALOGUEDEAD         1000
# define DEFAULTJOYSTICKANALOGUESATURATE     9500
#elif GAMEDEFS_JOYSTICK_DEFAULT == GAMEDEFS_JOYSTICK_DEFAULT_SENSITIVITY
# define DEFAULTJOYSTICKANALOGUESENSITIVITY  5.f
# define DEFAULTJOYSTICKANALOGUEDEAD         1000
# define DEFAULTJOYSTICKANALOGUESATURATE     9500
#else
# define DEFAULTJOYSTICKANALOGUESCALE        65536
# define DEFAULTJOYSTICKANALOGUEDEAD         1000
# define DEFAULTJOYSTICKANALOGUESATURATE     9500
#endif

#ifdef __cplusplus
}
#endif
#endif
