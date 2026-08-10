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

   CORE SW MULTIPLAYER VERIFIED in the unified binary: two `-game sw` instances on
   localhost (mmulti P2P, `-net -p<port> /n1 <peer:port>`) connect and run SW's
   pre-game sync handshake in lockstep — P0 logs `mmulti: player 1 connected!` and
   BOTH advance through `waitforeverybody() #1..#4` (game.cpp level-init barriers)
   at matching times. Confirms SW's `SW_PACKET` netcode + the shared mmulti
   transport (dual-stack/STUN/fast-send) work together for SW. Headless runs then
   stall at `InitLevel`/`LoadLevel`/precache (dummy video, same as the SP title
   stall), so full 2-player gameplay needs a real display, not a net fix.

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
   - [DONE-BACKEND] Masterserver client LOBBY QUERY. `Net_QueryLobby()` (weak
     `Masterserver_QueryList`) + `-lobbylist <addr>` arg. VERIFIED full round-trip:
     an SW `-advertise` server registered, then an SW `-lobbylist` client fetched
     it: `SW lobby: 1 server(s)` / `[0] ...:23513 "VibeBuild32 SW server" map=SW 1/1`.
   - [DONE] Lobby-based MATCHMAKING (discover + join). `-joinlobby <masteraddr>`
     (Net_SetAutoJoin/Net_ResolveAutoJoin) queries the masterserver, picks the first
     server, and feeds its address into SW's mmulti connect with NAT-punch armed;
     the host's connect-wait loop now also calls Net_HostAdvertise() so a waiting
     server is discoverable. VERIFIED full round-trip: an advertising SW host
     (`-advertise ... -net -p1234 /n0:2`) was discovered by an SW `-joinlobby`
     client which CONNECTED (`mmulti: player 1 connected!`) and began the
     `waitforeverybody()` sync handshake.
   - [TODO-UI] In-menu server BROWSER (graphical): wire the list into SW's menus.cpp
     for click-to-join. Backend + the whole discover/join path are done; this is
     pure SW menu-framework UI (unverifiable without a display).
   - [TODO] Mid-game join + file transfer with x-of-y. SCOPED: Duke's chunked
     transfer (`Net_SendFile`/`Net_SendGameFiles`, `Net_FindExistingFile` dedup by
     size+CRC, `oldnet.cpp:1267-1501`) is ~game-agnostic at the wire level
     (`mmulti_sendpacket` + `PACKET_TYPE_FILE_HEADER`/`_FILE_CHUNK`). Porting to SW
     needs: (a) add those two packet ids to SW's own packet-type space and handle
     them in SW's `getpackets()` switch (network.cpp:1296, `switch(packbuf[0])`)
     without colliding with SW's ids; (b) a mid-game late-join trigger — SW's
     connect is a fixed pre-game `waitforeverybody` handshake with no "joiner lacks
     the GRP, host streams it" path. Non-trivial multi-file port; can't verify
     headlessly (mid-game join needs real video past the level-load stall).
   - [TODO] Deterministic lockstep / rejoin. NOTE: SW ALREADY runs a working
     lockstep sync (`SW_PACKET` + `waitforeverybody`, verified above); our Duke
     variant's value is the rejoin/late-join fixes, which depend on the mid-game
     path above. A full swap of SW's sync model is a large, risky rewrite with
     little upside since SW's own lockstep already syncs cleanly.
   - [N/A] Enemy skins. ASSESSED not applicable: Duke's feature draws a PLAYER as a
     stock ENEMY tile (`P_SkinBaseTile`→NEWBEAST, `P_GetSkinFrame`, `P_ValidateSkin`)
     via its CON/actor art. SW has no CON, entirely different enemy art, and already
     differentiates multiplayer players by team-color palette swap
     (`pp->SpriteP->pal = PALETTE_PLAYER0 + pp->TeamColor`, network.cpp:497). Re-homing
     Duke's mechanic would be inventing a new SW feature, not porting ours — out of scope.

## Reference: game data
SW GRP: `/Users/alonamir/games/Shadow Warrior Complete/Sw.grp` (+ WT.GRP world tour).
