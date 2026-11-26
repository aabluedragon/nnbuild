// Shared Duke/NNBuild GRP loading and add-on selection.
//
// Define DUKE_COMMON_AFTER_COMMAND_GRP_LOAD(path) before including this file
// when a caller needs a side effect after a command-line GRP is loaded.

#ifndef DUKE_COMMON_AFTER_COMMAND_GRP_LOAD
# define DUKE_COMMON_AFTER_COMMAND_GRP_LOAD(path) do { (void)(path); } while (0)
#endif

// Define DUKE_COMMON_STANDALONE_PROJECT() to an expression that is true when
// the game runs from a self-contained project that brings its own data and
// needs no base GRP.
#ifndef DUKE_COMMON_STANDALONE_PROJECT
# define DUKE_COMMON_STANDALONE_PROJECT() 0
#endif

static int32_t G_TryLoadingGrp(char const * const grpfile)
{
    // An explicitly selected GRP may be a nested or absolute path.
    int const previousMode = pathsearchmode;
    pathsearchmode = 1;
    int32_t const i = initgroupfile(grpfile);
    pathsearchmode = previousMode;

    if (i == -1)
        LOG_F(WARNING, "Unable to open %s.", grpfile);
    else
        LOG_F(INFO, "Using %s as main data file.", grpfile);

    return i;
}

static int32_t G_LoadGrpDependencyChain(grpfile_t const * const grp)
{
    if (!grp)
        return -1;

    if (grp->type->dependency && grp->type->dependency != grp->type->crcval)
        G_LoadGrpDependencyChain(FindGroup(grp->type->dependency));

    int32_t const i = G_TryLoadingGrp(grp->filename);

    if (grp->type->postprocessing)
        grp->type->postprocessing(i);

    return i;
}

void G_LoadGroups(int32_t autoload)
{
    if (g_modDir[0] != '/')
    {
        char cwd[BMAX_PATH];

        Bstrcat(g_rootDir, g_modDir);
        addsearchpath(g_rootDir);
        //        addsearchpath(mod_dir);

        char path[BMAX_PATH];

        if (buildvfs_getcwd(cwd, BMAX_PATH))
        {
            Bsnprintf(path, sizeof(path), "%s/%s", cwd, g_modDir);
            if (!Bstrcmp(g_rootDir, path))
            {
                if (addsearchpath(path) == -2)
                    if (buildvfs_mkdir(path, S_IRWXU) == 0)
                        addsearchpath(path);
            }
        }

#ifdef USE_OPENGL
        Bsnprintf(path, sizeof(path), "%s/%s", g_modDir, TEXCACHEFILE);
        Bstrcpy(TEXCACHEFILE, path);
#endif
    }

    if (g_addonNum && !DUKE_COMMON_STANDALONE_PROJECT())
        G_LoadAddon();

    const char *grpfile;
    int32_t i;

    if (DUKE_COMMON_STANDALONE_PROJECT())
    {
        grpfile = "";
        i = -1;
        g_selectedGrp = nullptr;
        g_gameType = GAMEFLAG_DUKE;
    }
    else if ((i = G_LoadGrpDependencyChain(g_selectedGrp)) != -1)
    {
        grpfile = g_selectedGrp->filename;

        clearGrpNamePtr();
        g_grpNamePtr = dup_filename(grpfile);

        grpinfo_t const * const type = g_selectedGrp->type;

        g_gameType = type->game;
        g_gameNamePtr = type->name;

        if (type->scriptname && g_scriptNamePtr == NULL)
            g_scriptNamePtr = dup_filename(type->scriptname);

        if (type->defname && g_defNamePtr == NULL)
            g_defNamePtr = dup_filename(type->defname);

        if (type->rtsname && g_rtsNamePtr == NULL)
            g_rtsNamePtr = dup_filename(type->rtsname);
    }
    else
    {
        grpfile = G_GrpFile();
        i = G_TryLoadingGrp(grpfile);
    }

    if (autoload)
    {
        G_LoadGroupsInDir("autoload");

        if (i != -1)
            G_DoAutoload(grpfile);
    }

    if (g_modDir[0] != '/')
        G_LoadGroupsInDir(g_modDir);

#ifndef EDUKE32_STANDALONE
    if (g_defNamePtr == NULL)
    {
        const char *tmpptr = getenv("DUKE3DDEF");
        if (tmpptr)
        {
            clearDefNamePtr();
            g_defNamePtr = dup_filename(tmpptr);
            LOG_F(INFO, "Using %s as definitions file (DUKE3DDEF env var override).", g_defNamePtr);
        }
    }
#endif

    loaddefinitions_game(G_DefFile(), TRUE);

    struct strllist *s;

    int const bakpathsearchmode = pathsearchmode;
    pathsearchmode = 1;

    while (CommandGrps)
    {
        int32_t j;

        s = CommandGrps->next;

        if ((j = initgroupfile(CommandGrps->str)) == -1)
            LOG_F(ERROR, "Unable to load %s: file not found.", CommandGrps->str);
        else
        {
            g_groupFileHandle = j;
            LOG_F(INFO, "Loaded %s", CommandGrps->str);
            if (autoload)
                G_DoAutoload(CommandGrps->str);

            DUKE_COMMON_AFTER_COMMAND_GRP_LOAD(CommandGrps->str);
        }

        Xfree(CommandGrps->str);
        Xfree(CommandGrps);
        CommandGrps = s;
    }
    pathsearchmode = bakpathsearchmode;
}

static void G_LoadAddon(void)
{
#ifndef EDUKE32_STANDALONE
    uint32_t crc;

    switch (g_addonNum)
    {
    case ADDON_DUKEDC:
        crc = DUKEDC_CRC;
        break;
    case ADDON_NWINTER:
        crc = DUKENW_CRC;
        break;
    case ADDON_CARIBBEAN:
        crc = DUKECB_CRC;
        break;
    default:
        return;
    }

    grpfile_t const * const grp = FindGroup(crc);

    if (grp)
        g_selectedGrp = grp;
#endif
}
