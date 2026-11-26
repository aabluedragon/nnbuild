// Shared Mapster command-line help dialog. Blood uses the same option text with
// a different executable/title name.
#ifndef M32_PARAMETER_HELP_USAGE_NAME
# define M32_PARAMETER_HELP_USAGE_NAME "mapster32"
#endif
#ifndef M32_PARAMETER_HELP_TITLE_NAME
# define M32_PARAMETER_HELP_TITLE_NAME "Mapster32"
#endif

static void G_ShowParameterHelp(void)
{
    const char *s = "Usage: " M32_PARAMETER_HELP_USAGE_NAME " [files] [options]\n\n"
              "-g [file.grp], -grp [file.grp]\tLoad extra group file\n"
              "-h [file.def]\t\tLoad an alternate definitions file\n"
              "-x [game.con]\t\tLoad a custom CON script for getting sound definitions\n"
              "-mh [file.def]\t\tInclude additional definitions module\n"
              "-mx [file.con]\t\tInclude additional CON module for getting sound definitions\n"
              "-j [dir], -game_dir [dir]\n"
              "\t\t\tAdds a directory to the file path stack\n"
              "-cachesize #\t\tSets cache size, in Kb\n"
              "-check\t\t\tEnables map pointer checking when saving\n"
#ifdef HAVE_CLIPSHAPE_FEATURE
              "-clipmap [file.map]\t\tLoad an additional clipping map for use with clipshape\n"
#endif
              "-namesfile [file.h]\t\tLoad a custom NAMES.H for tile names\n"
              "-nocheck\t\t\tDisables map pointer checking when saving (default)\n"  // kept for script compat
#ifdef STARTUP_SETUP_WINDOW
              "-setup\t\t\tDisplays the configuration dialog\n"
#endif
              "-usecwd\t\t\tRead game data and configuration file from working directory\n"
              "\n-?, -help, --help\t\tDisplay this help message and exit"
              ;
    Bsprintf(tempbuf, M32_PARAMETER_HELP_TITLE_NAME " %s", s_buildRev);
    wm_msgbox(tempbuf, "%s", s);
}

#undef M32_PARAMETER_HELP_USAGE_NAME
#undef M32_PARAMETER_HELP_TITLE_NAME
