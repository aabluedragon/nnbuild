# Powerslave/Exhumed in the VibeBuild32 unified binary

Powerslave/Exhumed support uses the PCExhumed runtime imported from NBlood's
`source/exhumed` tree. It is not a Duke/NAM-family variant, so it cannot reuse
`Duke_app_main` the way NAM, Napalm, and World War II GI do.

The integration follows the existing non-duplicating merged-module pattern:

1. Compile the PCExhumed game objects into `obj/exhumed/`.
2. Prelink them into `obj/exhumed_merged.o` with the generic `MERGE_RULE` in
   `GNUmakefile`.
3. Localize symbols that collide with the already-linked games.
4. Dispatch only the shared engine-facing callbacks from
   `source/vibebuild32/src/gameselect.cpp`.

The exported callbacks are renamed in the imported source:

| canonical engine name | PCExhumed symbol              | file             |
|-----------------------|-------------------------------|------------------|
| `app_main`            | `Exhumed_app_main`            | `src/exhumed.cpp` |
| `G_DefaultDefFile`    | `Exhumed_G_DefaultDefFile`    | `src/common.cpp`  |
| `faketimerhandler`    | `Exhumed_faketimerhandler`    | `src/exhumed.cpp` |

Selection is handled by shared selector metadata:

- `-gm exhumed`, `-gm powerslave`, `-gm power-slave`, `-gm "power slave"`, or
  `-gm pcexhumed`
- `STUFF.DAT` basename inference
- PCExhumed/Powerslave full and demo data-file signatures from
  `source/exhumed/src/grpscan.cpp`

Future Build-engine game additions should follow the same rule used here and in
the Duke/NAM-family selector entries: reuse an existing runtime when it already
supports the game, and add a new merged module only when the game has its own
runtime that cannot safely run inside an existing one.

## Bring-up fixes

Three defects kept a Powerslave/Exhumed launch from reaching gameplay in the
unified binary:

- **No ART loaded at all.** `InstallEngine()` called
  `artLoadFiles("tiles%03i.art", ...)`. NBlood's engine formats that string;
  eduke32's `artGetIndexedFileName()` instead substitutes digits *positionally*
  into a literal `tilesNNN.art` name, so the format string became
  `tiles000i.art` and no ART file was ever opened. Every `tilesiz` stayed 0x0,
  so nothing drew, and `PlayMovie()` then crashed writing the first frame
  through a null `waloff[kMovieTile]`. The call now passes the literal
  `"tiles000.art"`, exactly as the Blood and RR imports were fixed.
- **First-run hang on the startup window.** `Exhumed_app_main` gated
  `startwin_run()` on `readSetup < 0` regardless of `-nosetup`, so a fresh
  config blocked on a setup dialog this build does not ship for Exhumed. The
  first-run term now respects `g_noSetup`, matching SW, Blood and RR.
- **No way to verify a run without a human at the keyboard.** `GameDisplay()`
  honours `VB_CP_CAPTURE=<frames>`, the same debug hook SW and the Duke runtime
  use: it saves one screenshot that many drawn frames in.

Verified on the Powerslave **demo** (`STUFF.DAT`, 15904838 bytes, CRC
`0x1D8C7645`): logo -> title screens -> `BOOK.MOV` -> menu all run, and
`-1` drops straight into LEV1 with correct geometry, sprites, weapon and status
bar.

Known demo-data limits (not bugs): the demo ships no CD audio, so
`playCDtrack()` logs `Error opening music track NN` and plays nothing.

### Running it

```
./vibebuild32 -gm exhumed -g /path/to/powerslave/STUFF.DAT -nosetup
```

`-gm exhumed` is optional when the `-g` path names `STUFF.DAT` -- the selector
infers the game from the basename and from the full/demo data-file signatures.
