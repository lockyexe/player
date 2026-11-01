# Macro Player

Plays back `.gdr` macros exactly as they were recorded. Nothing more.

- Every recorded press and release is fed to the game on the physics tick it was recorded on.
- Holds last exactly as long as recorded; simultaneous inputs and overlapping holds (both players, jump/left/right) are kept, in file order.
- While a macro plays, it is the only source of input.
- Playback stops on its own when the macro ends, the player dies, the level is completed or you leave the level.

No AI, no auto-play, no input guessing, no position corrections.

## Use

Press **Caps Lock** to open or close the menu. Nothing is drawn on screen while it is closed.

- **Import** copies a `.gdr`, `.gdr2` or `.json` macro into the macro folder and selects it.
- **< / >** switch between imported macros. **Folder** opens the macro folder.
- **Play** restarts the current level and plays from the first tick. If you are not in a level, playback starts when the next attempt begins. **Stop** ends playback and releases any held buttons.
- Restarting the level during playback replays the macro from the start.

## Supported files

- GDR2 (binary, `GDR` header), including files with bot extensions or per-input physics data (extra data is skipped)
- GDR1 msgpack `.gdr` and GDR1 JSON (`.gdr.json`)
- xdBot macros are timed the way xdBot records them (level time) and use xdBot's player mapping

The game must run at the TPS the macro was recorded at; the menu warns when a macro was not recorded at 240 TPS.
