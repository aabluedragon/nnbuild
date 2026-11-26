// Shared Mapster32 OSD command registration used by Duke, Blood, and Redneck Rampage.
// Include with MAPSTER_SHARED_REGISTER_OSD_COMMANDS defined.

#if defined(MAPSTER_SHARED_REGISTER_OSD_COMMANDS)
    OSD_RegisterFunction("addpath","addpath <path>: adds path to game filesystem", osdcmd_addpath);

    OSD_RegisterFunction("editorgridextent","editorgridextent: sets the size of the 2D mode editing grid",osdcmd_editorgridextent);

    OSD_RegisterFunction("initgroupfile","initgroupfile <path>: adds a grp file into the game filesystem", osdcmd_initgroupfile);

    OSD_RegisterFunction("m32_clipping","m32_clipping: toggles clipping mode", osdcmd_noclip);

    OSD_RegisterFunction("quit","quit: exits the editor immediately", osdcmd_quit);
    OSD_RegisterFunction("exit","exit: exits the editor immediately", osdcmd_quit);

#ifdef MAPSTER_REGISTER_ARTDUMP_COMMAND
    OSD_RegisterFunction("artdump","dump art to disk", osdcmd_artdump);
#endif

    OSD_RegisterFunction("sensitivity","sensitivity <value>: changes the mouse sensitivity", osdcmd_sensitivity);

#ifdef MAPSTER_REGISTER_UPSCALEFACTOR_COMMAND
    OSD_RegisterFunction("upscalefactor","upscalefactor <value>: integer upscaling",osdcmd_upscalefactor);
#endif

    //PK
    OSD_RegisterFunction("m32_2d3dmode", "2d3dmode: experimental 2d/3d hybrid mode", osdcmd_vars_pk);
    OSD_RegisterFunction("pointhighlightdist", "pointhighlightdist: distance at which points are selected", osdcmd_vars_pk);
    OSD_RegisterFunction("linehighlightdist", "linehighlightdist: distance at which lines are selected", osdcmd_vars_pk);
    OSD_RegisterFunction("pk_turnaccel", "pk_turnaccel <value>: sets turning acceleration+deceleration", osdcmd_vars_pk);
    OSD_RegisterFunction("pk_turndecel", "pk_turndecel <value>: sets turning deceleration", osdcmd_vars_pk);
    OSD_RegisterFunction("pk_uedaccel", "pk_uedaccel <value>: sets UnrealEd movement speed factor (0-5, exponentially)", osdcmd_vars_pk);
    OSD_RegisterFunction("pk_quickmapcycling", "pk_quickmapcycling: toggles quick cycling of maps with (Shift-)Ctrl-X", osdcmd_vars_pk);
    OSD_RegisterFunction("testplay_addparam", "testplay_addparam \"string\": sets additional parameters for test playing", osdcmd_testplay_addparam);
    OSD_RegisterFunction("show_heightindicators", "show_heightindicators {0, 1 or 2}: sets display of height indicators in 2D mode", osdcmd_vars_pk);
    OSD_RegisterFunction("show_ambiencesounds", "show_ambiencesounds {0, 1 or 2}: sets display of MUSICANDSFX circles in 2D mode", osdcmd_vars_pk);
    OSD_RegisterFunction("corruptcheck_noalreadyrefd", "corruptcheck_noalreadyrefd: toggles ignoring of one-to-many red wall connections", osdcmd_vars_pk);
    OSD_RegisterFunction("corruptcheck_game_duke3d", "corruptcheck_game_duke3d: toggles ignoring of Duke3D issues", osdcmd_vars_pk);
    OSD_RegisterFunction("corruptcheck_heinum", "corruptcheck_heinum: toggles auto-correcting inconsistent c/fstat bit 2 and heinum (2: also warn)", osdcmd_vars_pk);
    OSD_RegisterFunction("keeptexturestretch", "keeptexturestretch: toggles keeping texture stretching when dragging wall vertices", osdcmd_vars_pk);

    OSD_RegisterFunction("corruptcheck", "corruptcheck {<seconds>|now|tryfix}: sets auto corruption check interval if <seconds> given, otherwise as indicated", osdcmd_vars_pk);
#ifdef USE_OPENGL
    OSD_RegisterFunction("tint", "tint <pal> <r> <g> <b> <flags>: queries or sets hightile tinting", osdcmd_tint);
#endif

#if defined(MAPSTER_REGISTER_LUA_COMMAND) && defined(LUNATIC)
    OSD_RegisterFunction("lua", "lua \"Lua code...\": runs Lua code", osdcmd_lua);
#endif

    // M32 script
    OSD_RegisterFunction("include", "include <filenames...>: compiles one or more M32 script files", osdcmd_include);
    OSD_RegisterFunction("do", "do (m32 script ...): executes M32 script statements", osdcmd_do);
    OSD_RegisterFunction("script_info", "script_info: shows information about compiled M32 script", osdcmd_scriptinfo);
    OSD_RegisterFunction("script_expertmode", "script_expertmode: toggles M32 script expert mode", osdcmd_vars_pk);
    OSD_RegisterFunction("enableevent", "enableevent {all|EVENT_...|(event number)}", osdcmd_endisableevent);
    OSD_RegisterFunction("disableevent", "disableevent {all|EVENT_...|(event number)}", osdcmd_endisableevent);
    OSD_RegisterFunction("osd_tryscript", "osd_tryscript: toggles execution of M32 script on invalid OSD command", osdcmd_vars_pk);
    OSD_RegisterFunction("sideview_reversehorizrot", "sideview_reversehorizrot: toggles reversion of Q and W keys in side view mode", osdcmd_vars_pk);
#ifdef DEBUGGINGAIDS
    OSD_RegisterFunction("disasm", "disasm [s|e] <state or event number>", osdcmd_disasm);
#endif

#undef MAPSTER_REGISTER_LUA_COMMAND
#undef MAPSTER_REGISTER_UPSCALEFACTOR_COMMAND
#undef MAPSTER_REGISTER_ARTDUMP_COMMAND
#undef MAPSTER_SHARED_REGISTER_OSD_COMMANDS
#endif
