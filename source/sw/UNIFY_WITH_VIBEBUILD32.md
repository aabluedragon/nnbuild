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
2. (done) Glue TU + game selector; wire the combined link; reach a clean link.
3. (done) Runtime bring-up: `-game sw` reaches SW's title/intro screen end-to-end
   inside the combined `vibebuild32` binary — selector chdir → `SW_app_main` →
   engine init → "SHADOW WARRIOR(tm) Version 1.2" → 32M cache → title-screen
   event loop (`while (!KeyPressed()) handleevents()`). Verified via stack sample
   (main thread idling in `SDL_WaitEventTimeout` from SW's intro wait), headless.
   Duke (`-game duke`) unchanged (E1L1). Default `make vibebuild32` still Duke-only.
4. (done) Both games launch from the one binary. NOTE — first-run friction: SW's
   setup gate (`game.cpp:3402`, `if (i<0 || CommandSetup || (ForceSetup&&!g_noSetup))`)
   forces the shared (Duke-provided) Cocoa startup window when `CONFIG_ReadSetup()`
   returns <0, i.e. no `~/.config/voidsw/voidsw.cfg` yet — identical to Duke's own
   first-run. Seed a `voidsw.cfg` with `[Setup] ForceSetup=0 / SelectedGRP="Sw.grp"`
   (or click Start once) and SW auto-launches thereafter. `-nosetup` alone does NOT
   bypass it because the `i<0` (missing-config) term ignores `g_noSetup`.
5. IN PROGRESS: port vibebuild32 features onto SW's game code, one at a time.
   SW's net model is its own `SW_PACKET` delta-input system (`network.cpp`, 1657 LOC,
   `EncodeBits`/`DecodeBits`), NOT Duke's `oldnet.cpp` (4153 LOC) that we extended
   with deterministic lockstep + masterserver/IPv6/STUN/mid-game transfer/skins.
   Each feature is Duke-game-specific and must be re-homed onto SW's net path.

   What's already SHARED (free for SW via the engine, no port needed):
   - `mmulti.cpp` transport: dual-stack IPv4/IPv6 peers, STUN (`mmulti_punchto_str`/
     `mmulti_sendraw_str`), fast whole-window send (`dosendpackets`,
     `NET_MAX_PACKETS_PER_CALL`). SW already routes packets through mmulti.
   - `masterserver.cpp` module: game-agnostic; only in Duke's obj list, so its
     `Masterserver_*` symbols are external and callable from SW in the combined link.

   Feature-port checklist onto SW:
   - [DONE] Masterserver/lobby ADVERTISE (host side) + NAT-punch service. SW's
     `network.cpp` calls the `Masterserver_*` API weak-linked (null in standalone
     sw, bound to Duke's in the combined binary); pulses from `getpackets()`;
     `-advertise <addr>` arg; `atexit` graceful stop. VERIFIED end-to-end: a
     standalone `-masterserver` registered an SW `-advertise` client:
     `masterserver: + 127.0.0.1:PORT "VibeBuild32 SW server" (SW)` (dual-stack).
   - [DONE] Client NAT hole-punch on join. SW's `initmultiplayerscycle()` connect
     loop (game.cpp:876) now calls `Net_ClientPunchAttempt()` — throttled, builds
     the request via weak `Masterserver_MakePunchReq` + shared `mmulti_sendraw_str`.
     New `-punchto <server>` / `-masteraddr <ms>` args (must precede `-net`).
     VERIFIED: an SW client auto-starting a net game toward an unreachable server
     logs `SW: requested NAT punch to <target> via masterserver <ep>` while waiting.
   - [TODO] Masterserver client BROWSER (in-menu server list + pick-to-join). Needs
     SW menu wiring; the query/response wire format is game-agnostic in masterserver.cpp.
   - [TODO] Mid-game join + file transfer with x-of-y (Duke `oldnet.cpp`, tied to
     Duke's connection protocol; SW has its own connect handshake).
   - [TODO] Deterministic lockstep / rejoin (the big one — replaces SW's
     `SW_PACKET` sync model).
   - [TODO] Enemy skins (Duke CON/actor-specific; SW's actors differ — may not map).

## Reference: game data
SW GRP: `/Users/alonamir/games/Shadow Warrior Complete/Sw.grp` (+ WT.GRP world tour).
