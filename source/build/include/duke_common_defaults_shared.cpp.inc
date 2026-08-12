// Shared Duke/VibeBuild32 default grp/def/con handling.
//
// Define DUKE_COMMON_DEFAULT_DEF_FILE_SYMBOL before including this file when
// the Duke default-def implementation needs a non-canonical exported name.

#ifndef DUKE_COMMON_DEFAULT_DEF_FILE_SYMBOL
# define DUKE_COMMON_DEFAULT_DEF_FILE_SYMBOL G_DefaultDefFile
#endif

struct grpfile_t const *g_selectedGrp;

int32_t g_gameType = GAMEFLAG_DUKE;
int     g_addonNum = 0;

// g_gameNamePtr can point to one of: grpfiles[].name (string literal), string
// literal, malloc'd block (XXX: possible leak)
const char *g_gameNamePtr = NULL;

// grp/con handling

static const char *defaultconfilename                = "GAME.CON";
#ifndef EDUKE32_STANDALONE
static const char *defaultgamegrp[GAMECOUNT]         = { "DUKE3D.GRP", "NAM.GRP", "NAPALM.GRP", "WW2GI.GRP" };
static const char *defaultdeffilename[GAMECOUNT]     = { "duke3d.def", "nam.def", "napalm.def", "ww2gi.def" };
static const char *defaultgameconfilename[GAMECOUNT] = { "EDUKE.CON", "NAM.CON", "NAPALM.CON", "WW2GI.CON" };
#endif

char *g_grpNamePtr = NULL;
char *g_scriptNamePtr = NULL;
char *g_rtsNamePtr = NULL;

void clearGrpNamePtr(void)
{
    Xfree(g_grpNamePtr);
    // g_grpNamePtr assumed to be assigned to right after
}

void clearScriptNamePtr(void)
{
    Xfree(g_scriptNamePtr);
    // g_scriptNamePtr assumed to be assigned to right after
}

const char *G_DefaultGrpFile(void)
{
#ifndef EDUKE32_STANDALONE
    if (DUKE)
        return defaultgamegrp[GAME_DUKE];
    else if (NAPALM)
        return defaultgamegrp[GAME_NAPALM];
    else if (WW2GI)
        return defaultgamegrp[GAME_WW2GI];
    else if (NAM)
        return defaultgamegrp[GAME_NAM];

    return defaultgamegrp[0];
#else
    return "(none)";
#endif
}

const char *DUKE_COMMON_DEFAULT_DEF_FILE_SYMBOL(void)
{
#ifndef EDUKE32_STANDALONE
    if (DUKE)
        return defaultdeffilename[GAME_DUKE];
    else if (WW2GI)
        return defaultdeffilename[GAME_WW2GI];
    else if (NAPALM)
    {
        if (!testkopen(defaultdeffilename[GAME_NAPALM],0) && testkopen(defaultdeffilename[GAME_NAM],0))
            return defaultdeffilename[GAME_NAM]; // NAM/NAPALM Sharing
        else
            return defaultdeffilename[GAME_NAPALM];
    }
    else if (NAM)
    {
        if (!testkopen(defaultdeffilename[GAME_NAM],0) && testkopen(defaultdeffilename[GAME_NAPALM],0))
            return defaultdeffilename[GAME_NAPALM]; // NAM/NAPALM Sharing
        else
            return defaultdeffilename[GAME_NAM];
    }

    return defaultdeffilename[0];
#else
    return "(none)";
#endif
}

const char *G_DefaultConFile(void)
{
#ifndef EDUKE32_STANDALONE
    if (DUKE && testkopen(defaultgameconfilename[GAME_DUKE],0))
        return defaultgameconfilename[GAME_DUKE];
    else if (WW2GI && testkopen(defaultgameconfilename[GAME_WW2GI],0))
        return defaultgameconfilename[GAME_WW2GI];
    else if (NAPALM)
    {
        if (!testkopen(defaultgameconfilename[GAME_NAPALM],0))
        {
            if (testkopen(defaultgameconfilename[GAME_NAM],0))
                return defaultgameconfilename[GAME_NAM]; // NAM/NAPALM Sharing
        }
        else
            return defaultgameconfilename[GAME_NAPALM];
    }
    else if (NAM)
    {
        if (!testkopen(defaultgameconfilename[GAME_NAM],0))
        {
            if (testkopen(defaultgameconfilename[GAME_NAPALM],0))
                return defaultgameconfilename[GAME_NAPALM]; // NAM/NAPALM Sharing
        }
        else
            return defaultgameconfilename[GAME_NAM];
    }
#endif
    return defaultconfilename;
}

const char *G_GrpFile(void)
{
    return (g_grpNamePtr == NULL) ? G_DefaultGrpFile() : g_grpNamePtr;
}

const char *G_DefFile(void)
{
    return (g_defNamePtr == NULL) ? G_DefaultDefFile() : g_defNamePtr;
}

const char *G_ConFile(void)
{
    return (g_scriptNamePtr == NULL) ? G_DefaultConFile() : g_scriptNamePtr;
}
