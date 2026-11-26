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

// True when a file with this checksum and size is a known base game archive
// (not an add-on), so a multiplayer peer is expected to own it already.
bool G_IsBaseGrp(uint32_t crc, int32_t size)
{
    for (auto const *grp = listgrps; grp; grp = grp->next)
        if (!grp->dependency && !(grp->game & GAMEFLAG_ADDON) &&
            static_cast<uint32_t>(grp->crcval) == crc && grp->size == size)
            return true;
    return false;
}
