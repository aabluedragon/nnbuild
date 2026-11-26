// Shared Duke-family GRP scanner translation-unit wrapper.

#include "baselayer.h"
#include "cache1d.h"
#include "compat.h"
#include "crc32.h"
#include "scriptfile.h"
#include "vfs.h"

#if __has_include("common_game.h")
# include "common_game.h"
#else
# include "../../duke3d/src/common_game.h"
#endif

#if __has_include("duke3d.h")
# include "duke3d.h"
#else
# include "../../duke3d/src/duke3d.h"
#endif

#if __has_include("grpscan.h")
# include "grpscan.h"
#else
# include "../../duke3d/src/grpscan.h"
#endif

#include "duke_grpscan_shared.cpp.inc"
