// Shared Android control declarations used by Duke3D and Redneck Rampage.
// The including game header must include its local compat.h, control.h, and
// in_android.h first so game-specific quoted includes resolve correctly.

#ifndef build_android_shared_h_
#define build_android_shared_h_

#ifdef __ANDROID__

extern void  CONTROL_Android_ClearButton(int32_t whichbutton);
extern void  CONTROL_Android_PollDevices(ControlInfo *info);
extern void  CONTROL_Android_SetLastWeapon(int w);

extern void  CONTROL_Android_ScrollMap(int32_t *angle,int32_t *x, int32_t *y, uint16_t *zoom );

#endif

#endif
