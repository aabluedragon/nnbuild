# Ashline Relay

A single-map standalone game for NNBuild. Recover a blue power cell from the coolant wing, then transmit from the terminal beyond the eastern uplink seal. The southern workshop and the arrival bay have spare supplies.

- Move with **WASD** and aim with the mouse. Fire with **left click**.
- Jump with **Space** and use the uplink terminal with **E**.
- Coolant burns on contact. The walkway is the safe route.

## Play and edit

From the repository root, use NNBuild to open the project. The editor can export a playable ZIP from the project settings. The ZIP's gameplay components require script permission.

```sh
./nnbuild --project examples/ashline-relay
./nnbuild --editor --project examples/ashline-relay
./nnbuild --allow-project-scripts --project "/path/to/Ashline Relay.zip"
```

Press **F5** in the editor to playtest or return to editing. The entry map is `Maps/Ashline.map`. The JavaScript components in `Scripts/Station.js` handle the blue cell, coolant, seal, and terminal. `Tiles.tsv` lists imported lumps and project-specific art by tile number.

## Art and audio

The palette, surface textures, character and item sprites, weapon graphics, sound effects, and music use resources from Freedoom Phase 2. The map layout and mission are original to Ashline Relay. Freedoom license and contributor notices are in `Attribution/`.
