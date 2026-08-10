# Unifying Shadow Warrior (VoidSW) into the vibebuild32 executable

Goal: one `vibebuild32` binary that runs **both** Duke3D (vibebuild32) and Shadow
Warrior (the VoidSW port of the original SW source), selectable at launch, sharing
the one engine (`source/build`), audiolib and mact. SW's game code is reused from
`source/sw/src` (already builds+links as the standalone `voidsw` target); the Duke
side is untouched.

## Collision surface (measured, arm64)
Both game modules build against the shared engine. Linking them together collides on
**54 external-strong symbols** plus the macOS startup-window ObjC classes. They split
cleanly into two groups (the engine/mact/audiolib either reference the symbol or not):

### [B] 41 game-internal collisions — the engine never calls these
Each game only calls its own copy. **Resolution: prelink the SW objects with
`ld -r` and localize (`-unexported_symbols_list`) these 41**, so SW keeps its own
copies internally and they stop colliding with Duke's. No source edits. (Proven:
41 external -> 0 after localize.)

- `CONFIG_AnalogNameToNum(char const*)`
- `CONFIG_AnalogNumToName(int)`
- `CONFIG_FunctionNameToNum(char const*)`
- `CONFIG_FunctionNumToName(int)`
- `CONFIG_ReadSetup()`
- `CONFIG_SetupJoystick()`
- `CONFIG_SetupMouse()`
- `RTS_GetSound(int)`
- `RTS_NumSounds()`
- `RTS_SoundLength(int)`
- `_AppProperName`
- `_AppTechnicalName`
- `_EpisodeNames`
- `_G_DefFile`
- `_bufferjitter`
- `_dashow2dsector`
- `_foundgrps`
- `_g_addonNum`
- `_g_grpNamePtr`
- `_g_noSetup`
- `_g_selectedGrp`
- `_g_useCwd`
- `_gamefunctions`
- `_movefifoplc`
- `_movefifosendplc`
- `_mymaxlag`
- `_otherminlag`
- `_ototalclock`
- `_pub`
- `_pus`
- `_quotebot`
- `_quotebotgoal`
- `_ready2send`
- `_screenpeek`
- `_sdlappicon`
- `_searchparent`
- `_searchsect`
- `_startwin_run`
- `_user_quote`
- `_user_quote_time`
- `initsynccrc()`

### [A] engine-referenced collisions — the shared engine/mact calls these
These must resolve to one definition that routes to the **active** game.
- `app_main` -> a new **game-selector** `app_main` (glue TU): detect the game
  (GRP autodetect / `-game sw`), set `g_activeGame`, call `Duke_app_main` or
  `SW_app_main`.
- `startwin_*` (6) + the ObjC `GameListSource`/`GrpFile`/`StartupWindow` classes
  -> **drop SW's startup-window objects** (`*.game.mm`, `startosx`/`startwin`,
  `startgtk`); use Duke's single startup window for both. Removes those collisions
  outright.
- `faketimerhandler`, `app_crashhandler`, `G_Polymer_UnInit`, `M32RunScript`,
  `osdcmd_restartvid`, `G_DefaultDefFile` -> **dispatch** on `g_activeGame`. SW's
  copies are renamed `SW_*` (compile the combined SW object set with `-D` on just
  these ~6) and the glue TU provides the canonical name that switches.

- `_G_DefaultDefFile`
- `_G_Polymer_UnInit`
- `_M32RunScript`
- `_app_crashhandler`
- `_app_main`
- `_faketimerhandler`
- `_startwin_close`
- `_startwin_idle`
- `_startwin_isopen`
- `_startwin_open`
- `_startwin_puts`
- `_startwin_settitle`
- `osdcmd_restartvid(osdfuncparm_t const*)`

## Build wiring
- New module `swmerged`: same sources as `sw`, compiled into its own objdir with
  the ~6 `-D` renames for [A]; post-link `ld -r ... -unexported_symbols_list` to
  localize the 41 [B] symbols; SW startup-window sources excluded.
- New glue TU `source/vibebuild32/src/gameselect.cpp`: `g_activeGame`, the selector
  `app_main`, and the [A] dispatchers.
- `vibebuild32` target links: Duke objects (with `-D` renames for the [A] callbacks
  to `Duke_*`) + `sw_merged.o` + `gameselect.o`.

## Staged plan
1. (done) Prove `ld -r` localize; enumerate + categorize the 54 collisions.
2. Glue TU + game selector; wire the combined link; reach a clean link.
3. Runtime bring-up: make the selected SW game actually init/loop/render/sound
   inside the combined process (the real work — one engine, two game states).
4. Test both games launch and play from the one binary (Duke unchanged; SW with
   your Sw.grp); 0 desync on the Duke side preserved.
5. Only then: port vibebuild32 features (netcode, masterserver, IPv6, transfer,
   skins) onto SW's game code, one at a time.

## Reference: game data
SW GRP: `/Users/alonamir/games/Shadow Warrior Complete/Sw.grp` (+ WT.GRP world tour).
