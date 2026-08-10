// VibeBuild32 compatibility stubs for NotBlood input/control extensions that our
// mact/engine doesn't provide. No-ops (Blood loses these controller niceties) so
// the Blood game module links against our tree. Part of the Blood module only.
#include "compat.h"
#include "baselayer.h"
#include "control.h"
#include "joystick.h"

void JOYSTICK_SetSnapZone(int32_t /*axis*/, uint16_t /*snap*/) {}
bool CONTROL_GetControllerAxisIsTwinAxisStick(int32_t /*whichaxis*/) { return false; }
void CONTROL_GetAxisHeatMap(int32_t /*nAxis*/, uint8_t * /*tilePtr*/, int32_t /*nWidth*/, int32_t /*nHeight*/,
                            int32_t /*nPalBase*/, int32_t /*nPalRange*/, bool /*bDithering*/) {}
void mouseMoveToCenter(void) {}
