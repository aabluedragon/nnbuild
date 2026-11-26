// NNBuild uses Duke's dynamic-name declarations plus a small set of
// additional sound symbols used by the embedded Duke-compatible runtime.
#pragma once

#include "../../duke3d/src/dnames.h"

extern const int g_dynTileListSize;

constexpr int16_t NEWBEAST_ROAM__       = 316;
constexpr int16_t NEWBEAST_RECOG__      = 317;
constexpr int16_t NEWBEAST_PAIN__       = 319;
constexpr int16_t NEWBEAST_DYING__      = 320;
constexpr int16_t PRED_ROAM__           = 110;
constexpr int16_t PRED_PAIN__           = 113;
constexpr int16_t PRED_DYING__          = 114;
constexpr int16_t PIG_ROAM__            = 120;
constexpr int16_t PIG_PAIN__            = 123;
constexpr int16_t PIG_DYING__           = 124;
constexpr int16_t DUKE_KILLED2__        = 203;

extern int16_t NEWBEAST_ROAM;
extern int16_t NEWBEAST_RECOG;
extern int16_t NEWBEAST_PAIN;
extern int16_t NEWBEAST_DYING;
extern int16_t DUKE_KILLED2;
extern int16_t PRED_ROAM;
extern int16_t PRED_PAIN;
extern int16_t PRED_DYING;
extern int16_t PIG_ROAM;
extern int16_t PIG_PAIN;
extern int16_t PIG_DYING;
