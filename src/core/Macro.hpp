#pragma once

// Platform-independent macro model. No Geode / GD headers in here so the
// parser and scheduler can be unit-tested on any machine.

#include <cstdint>
#include <string>
#include <vector>

namespace mp {

// Which game counter the macro's "frame" values refer to.
enum class FrameClock : uint8_t {
    // Physics step counter (GJGameState::m_currentProgress). This is what the
    // GDR spec and its reference bot use.
    PhysicsStep,
    // xdBot records frames as int(levelTime * tps) + 1 instead of the step
    // counter. Files whose bot name is "xdBot" use this clock.
    XdBotLevelTime,
};

// How the stored player flag maps onto handleButton's isPlayer1 argument.
enum class PlayerFlag : uint8_t {
    // GDR spec: flag is "player 2" -> isPlayer1 = !flag
    IsPlayer2,
    // xdBot stores the raw isPlayer1 argument it saw (XOR the "flip 2-player
    // controls" option when not in platformer); it replays it the same way.
    XdBotRaw,
};

enum class Format : uint8_t { GDR2, GDR1Msgpack, GDR1Json };

struct Input {
    uint64_t frame = 0;
    uint8_t button = 1;   // 1 = Jump, 2 = Left, 3 = Right (PlayerButton)
    bool player2 = false; // raw stored flag, interpret with PlayerFlag
    bool down = false;
};

struct Macro {
    Format format = Format::GDR2;
    int formatVersion = 0;

    std::string author;
    std::string description;
    std::string botName;
    std::string botVersion;
    std::string levelName;
    uint32_t levelId = 0;

    double framerate = 240.0;
    double duration = 0.0;    // as stored; units differ between bots
    double gameVersion = 0.0;
    bool platformer = false;
    bool ldm = false;

    FrameClock clock = FrameClock::PhysicsStep;
    PlayerFlag playerFlag = PlayerFlag::IsPlayer2;

    // Sorted by frame (stable: equal frames keep file order).
    std::vector<Input> inputs;

    // Diagnostics gathered while parsing (never used to change inputs).
    bool hadToSort = false;           // file wasn't in frame order
    size_t unmatchedPresses = 0;      // press with no later release
    size_t unmatchedReleases = 0;     // release while not held
    size_t repeatedPresses = 0;       // press while already held

    uint64_t lastFrame() const { return inputs.empty() ? 0 : inputs.back().frame; }
};

} // namespace mp
