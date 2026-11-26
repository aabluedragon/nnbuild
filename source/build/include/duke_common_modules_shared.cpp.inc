// Shared Duke/NNBuild command module and search-path helpers.
//
// Included from source/duke3d/src/common.cpp and
// source/nnbuild/src/common.cpp.

void G_CleanupSearchPaths(void)
{
    removesearchpaths_withuser(SEARCHPATH_REMOVE);

    if (!NAM)
        removesearchpaths_withuser(SEARCHPATH_NAM);

    if (!WW2GI)
        removesearchpaths_withuser(SEARCHPATH_WW2GI);

    if (!FURY)
        removesearchpaths_withuser(SEARCHPATH_FURY);
}

//////////

GrowArray<char *> g_scriptModules;

void G_AddGroup(const char *buffer)
{
    char buf[BMAX_PATH];

    struct strllist *s = (struct strllist *)Xcalloc(1,sizeof(struct strllist));

    Bstrcpy(buf, buffer);

    if (Bstrchr(buf,'.') == 0)
        Bstrcat(buf,".grp");

    s->str = Xstrdup(buf);

    if (CommandGrps)
    {
        struct strllist *t;
        for (t = CommandGrps; t->next; t=t->next) ;
        t->next = s;
        return;
    }
    CommandGrps = s;
}

void G_AddPath(const char *buffer)
{
    struct strllist *s = (struct strllist *)Xcalloc(1,sizeof(struct strllist));
    s->str = Xstrdup(buffer);

    if (CommandPaths)
    {
        struct strllist *t;
        for (t = CommandPaths; t->next; t=t->next) ;
        t->next = s;
        return;
    }
    CommandPaths = s;
}

void G_AddCon(const char *buffer)
{
    clearScriptNamePtr();
    g_scriptNamePtr = dup_filename(buffer);
    LOG_F(INFO, "Using CON file %s", g_scriptNamePtr);
}

void G_AddConModule(const char *buffer)
{
    g_scriptModules.append(Xstrdup(buffer));
}
