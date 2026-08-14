#define VIBEBUILD32_FURY_CRC (int32_t)0xE175FB41

#define DUKE_GRP_EXTRA_INTERNAL_ENTRIES \
    { "Ion Fury: Aftershock", VIBEBUILD32_FURY_CRC, 160826590, GAMEFLAG_FURY|GAMEFLAG_NOCLASSIC|GAMEFLAG_NOPOLYMER, 0, "scripts/main.con", NULL },

#define DUKE_GRP_EXTRA_SCRIPT_SYMBOLS \
    scriptfile_addsymbolvalue("GAMEFLAG_FURY", GAMEFLAG_FURY); \
    scriptfile_addsymbolvalue("GAMEFLAG_STANDALONE", GAMEFLAG_STANDALONE); \
    scriptfile_addsymbolvalue("GAMEFLAG_NOCLASSIC", GAMEFLAG_NOCLASSIC); \
    scriptfile_addsymbolvalue("FURY_CRC", VIBEBUILD32_FURY_CRC);

#include "../../build/include/duke_grpscan_wrapper.cpp.inc"

#undef DUKE_GRP_EXTRA_SCRIPT_SYMBOLS
#undef DUKE_GRP_EXTRA_INTERNAL_ENTRIES
#undef VIBEBUILD32_FURY_CRC
