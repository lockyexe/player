// Writes GDR2 fixture files with the official maxnut/GDReplayFormat (gdr2)
// library, then re-imports each with that same library and dumps the inputs
// it reads back. The test suite compares our parser against these dumps.
//
// usage: gdr2_generate <out-dir>

#include <gdr/gdr.hpp>
#include <gdr/physics_extension.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

struct PlainReplay : gdr::Replay<PlainReplay, gdr::Input<>> {
    PlainReplay() : Replay("FixtureBot", 3) {}
};

// Replay with both a replay-level extension block and per-input physics data,
// so the reader has to skip unknown extension bytes correctly.
struct PhysReplay : gdr::Replay<PhysReplay, PhysicsInput> {
    int attempts = 77;
    PhysReplay() : Replay("PhysBot", 9) {}
    void saveExtension(binary_writer& w) const override { w << attempts << std::string("extension-payload"); }
    void parseExtension(binary_reader& r) override { r >> attempts; }
    bool shouldParseExtension() const override { return botInfo.name == "PhysBot"; }
};

template <class R>
static bool writeAndDump(R& replay, fs::path const& dir, std::string const& name) {
    auto path = dir / (name + ".gdr");
    auto res = replay.exportData(path);
    if (res.isErr()) {
        std::fprintf(stderr, "%s: export failed: %s\n", name.c_str(), res.unwrapErr().c_str());
        return false;
    }
    auto back = R::importData(path);
    if (back.isErr()) {
        std::fprintf(stderr, "%s: reference import failed: %s\n", name.c_str(), back.unwrapErr().c_str());
        return false;
    }
    auto const& r = back.unwrap();
    std::ofstream out(dir / (name + ".expected"));
    out << "framerate " << r.framerate << "\n";
    out << "platformer " << (r.platformer ? 1 : 0) << "\n";
    out << "bot " << r.botInfo.name << "\n";
    out << "count " << r.inputs.size() << "\n";
    for (auto const& in : r.inputs)
        out << in.frame << " " << int(in.button) << " " << (in.player2 ? 1 : 0) << " " << (in.down ? 1 : 0) << "\n";
    std::printf("wrote %s (%zu inputs)\n", path.string().c_str(), r.inputs.size());
    return true;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s <out-dir>\n", argv[0]); return 2; }
    fs::path dir = argv[1];
    fs::create_directories(dir);
    bool ok = true;

    // 1. Classic one player: taps, a zero-length tap (press+release same tick),
    //    inputs on frame 0, long holds, back-to-back re-presses.
    {
        PlainReplay r;
        r.author = "fixture"; r.levelInfo = gdr::Level("classic 1p", 1234);
        r.duration = 12.5f; r.gameVersion = 22081; r.framerate = 240.0;
        auto add = [&](uint64_t f, bool down) { r.inputs.emplace_back(f, 1, false, down); };
        add(0, true); add(0, false);          // zero-length tap on the very first tick
        add(1, true); add(64, false);         // delta crossing the 6-bit single-byte limit
        add(64, true); add(65, false);        // re-press on the release tick
        add(500, true); add(2900, false);     // 10 second hold
        add(2901, true); add(2902, false);
        ok &= writeAndDump(r, dir, "gdr2_classic_1p");
    }

    // 2. Classic two player: simultaneous presses and overlapping holds.
    {
        PlainReplay r;
        r.levelInfo = gdr::Level("classic 2p", 99);
        r.inputs = {
            {100, 1, false, true}, {100, 1, true, true},   // both players press on the same tick
            {150, 1, true, false},                         // p2 lets go while p1 keeps holding
            {175, 1, true, true},
            {300, 1, false, false},                        // p1 releases mid p2-hold
            {320, 1, true, false},
            {400, 1, true, true}, {400, 1, false, true},   // stored p2-before-p1 within the tick
            {401, 1, false, false}, {401, 1, true, false},
        };
        r.deaths = {50, 360};
        ok &= writeAndDump(r, dir, "gdr2_classic_2p");
    }

    // 3. Platformer: jump/left/right overlapping for both players.
    {
        PlainReplay r;
        r.platformer = true;
        r.levelInfo = gdr::Level("platformer", 7);
        r.inputs = {
            {0, 2, false, true},                          // hold left from tick 0
            {10, 1, false, true}, {10, 3, true, true},    // p1 jump + p2 right same tick
            {14, 1, false, false},
            {15, 3, false, true},                         // right while left still held
            {16, 2, false, false},
            {16, 1, true, true}, {16, 3, true, false},
            {40, 1, true, false},
            {41, 3, false, false},
            {41, 2, false, true}, {41, 1, false, true},   // two presses and a release on one tick
            {9000, 2, false, false}, {9000, 1, false, false},
        };
        ok &= writeAndDump(r, dir, "gdr2_platformer");
    }

    // 4. Physics-extension inputs + replay extension + large multi-byte frames.
    {
        PhysReplay r;
        r.levelInfo = gdr::Level("physics ext", 4242);
        r.framerate = 480.0;
        r.inputs.emplace_back(3, 1, false, true, 1.f, 2.f, 3.f, 4.0, 5.0);
        r.inputs.emplace_back(70000, 1, false, false, 1.f, 2.f, 3.f, 4.0, 5.0);
        r.inputs.emplace_back(70000, 1, false, true, 0.f, 0.f, 0.f, 0.0, 0.0);
        r.inputs.emplace_back(3000000, 1, false, false, 9.f, 9.f, 9.f, 9.0, 9.0); // ~3.5h; the reference reader overflows past 2^31
        ok &= writeAndDump(r, dir, "gdr2_physics_ext");
    }

    // 5. Only player 2 inputs (p1 count = 0 edge case).
    {
        PlainReplay r;
        r.inputs = {{20, 1, true, true}, {30, 1, true, false}};
        ok &= writeAndDump(r, dir, "gdr2_only_p2");
    }

    return ok ? 0 : 1;
}
