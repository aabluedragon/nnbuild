// Shared Duke/VibeBuild32 group/autoload/lookup loading helpers.
//
// Included from source/duke3d/src/common.cpp and
// source/vibebuild32/src/common.cpp.

// loads all group (grp, zip, pk3/4) files in the given directory
void G_LoadGroupsInDir(const char *dirname)
{
    static const char *extensions[] = { "*.grp", "*.zip", "*.ssi", "*.pk3", "*.pk4" };
    char buf[BMAX_PATH];
    fnlist_t fnlist = FNLIST_INITIALIZER;

    for (auto & extension : extensions)
    {
        BUILDVFS_FIND_REC *rec;

        fnlist_getnames(&fnlist, dirname, extension, -1, 0);

        for (rec=fnlist.findfiles; rec; rec=rec->next)
        {
            Bsnprintf(buf, sizeof(buf), "%s/%s", dirname, rec->name);
            LOG_F(INFO, "Using group file %s", buf);
            initgroupfile(buf);
        }

        fnlist_clearnames(&fnlist);
    }
}

void G_DoAutoload(const char *dirname)
{
    char buf[BMAX_PATH];

    Bsnprintf(buf, sizeof(buf), "autoload/%s", dirname);
    G_LoadGroupsInDir(buf);
}

//////////

void G_LoadLookups(void)
{
    int32_t j;
    buildvfs_kfd fp;

    if ((fp=kopen4loadfrommod("lookup.dat",0)) == buildvfs_kfd_invalid)
        if ((fp=kopen4loadfrommod("lookup.dat",1)) == buildvfs_kfd_invalid)
            return;

    j = paletteLoadLookupTable(fp);

    if (j < 0)
    {
        if (j == -1)
            LOG_F(ERROR, "Failed loading \"lookup.dat\": not enough data.");

        return kclose(fp);
    }

    uint8_t paldata[768];

    for (j=1; j<=5; j++)
    {
        // Account for TITLE and REALMS swap between basepal number and on-disk order.
        int32_t basepalnum = (j == 3 || j == 4) ? 4+3-j : j;

        if (kread_and_test(fp, paldata, 768))
            return kclose(fp);

        for (unsigned char & k : paldata)
            k <<= 2;

        paletteSetColorTable(basepalnum, paldata);
    }

    kclose(fp);
}
