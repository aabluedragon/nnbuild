int loaddefinitions_game(const char *fn, int32_t preload)
{
    scriptfile *script;

    UNREFERENCED_PARAMETER(preload);

    script = scriptfile_fromfile(fn);
    if (script)
        parsegroupfiles(script);

    for (char const * m : g_defModules)
        parsegroupfiles_include(m, NULL, "null");

    if (script)
        scriptfile_close(script);

    scriptfile_clearsymbols();

    return 0;
}
