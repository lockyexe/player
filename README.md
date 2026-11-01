# Macro Player

A Geode mod for Geometry Dash 2.2081 that replays `.gdr` macros input-for-input. See [about.md](about.md) for what it does in game.

## Building the .geode

Pushing to GitHub runs `.github/workflows/build.yml`, which:

1. builds and runs the parser/playback tests (`tests/`), and
2. builds the mod for Windows, macOS and Android with `geode-sdk/build-geode-mod`, then combines them into one `lockyexe.macro-player.geode`.

Download it from the run's **Macro Player** artifact. Pushing a tag like `v1.0.0` also attaches it to a GitHub release.

Locally (with the [Geode CLI and SDK](https://docs.geode-sdk.org/getting-started/) installed):

```sh
geode build
```

## How playback is timed

`GJBaseGameLayer::processCommands` runs once per physics tick. After the game's own commands run, the mod reads the macro clock and sends every input whose recorded frame has been reached to `GJBaseGameLayer::handleButton`, in file order. This is the same timing the GDR reference bot uses, so a press recorded on frame N is replayed on frame N, and a hold of K frames lasts K ticks.

- Macro clock: the physics step counter (`m_currentProgress`) for GDR files; `int(levelTime * tps) + 1` for files recorded by xdBot, because that is how xdBot writes frames.
- Player mapping: GDR's `2p` flag means player 2 (`isPlayer1 = !2p`). xdBot stores the raw `isPlayer1` argument it saw (XOR the "flip 2-player controls" option), so its files are replayed the same way xdBot replays them.
- During playback, all other `handleButton` calls on the level are dropped, so only the macro drives the player.
- Inputs are stable-sorted by frame on load (the GDR2 spec requires this; GDR1 files are only reordered if they were stored out of order, which none of the tested files were). Inputs on the same frame keep their file order.

## Tests

```sh
cmake -S tests -B build-tests -DCMAKE_CXX_COMPILER=g++
cmake --build build-tests
./build-tests/gdr2_generate build-tests/fixtures
./build-tests/macro_tests build-tests/fixtures tests/samples
```

- `gdr2_generate` writes GDR2 fixtures (one-player, two-player with simultaneous presses and overlapping holds, platformer with all three buttons, physics-extension files, player-2-only) with the official [GDReplayFormat](https://github.com/maxnut/GDReplayFormat) library and records what that library reads back.
- `gdr1_dump` reads GDR1 files with the official GDR1 library, for comparing against real macros.
- `macro_tests` parses every file, checks every input against the reference dump, then runs it through a simulated tick loop (with extra half-tick calls) and verifies each press/release fires once, in order, at its recorded frame, that every hold has its recorded length, that dying stops playback and releases exactly what was held, and that restarting replays identically. It also checks that malformed files are rejected.

`tests/samples` holds real macros from [macrodash](https://github.com/downupstudio/macrodash) (MIT, see `tests/samples/NOTICE`).
