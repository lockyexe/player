// Parser + playback verification for Macro Player.
//
// For every macro file in the given directories:
//   1. parse it with our parser
//   2. if "<name>.expected" exists (written by the official GDR libraries),
//      compare every input against it
//   3. run it through a simulated GD physics loop (one processCommands call per
//      tick, plus extra half-tick calls) and verify that every press, hold and
//      release lands on the right tick, in order, exactly once
//   4. verify that stopping on death releases exactly what was held and fires
//      nothing afterwards, and that restarting replays identically
//
// usage: macro_tests <dir> [<dir> ...]

#include "../src/core/MacroParser.hpp"
#include "../src/core/Playback.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace mp;

static int g_failures = 0;

#define CHECK(cond, ...)                                    \
    do {                                                    \
        if (!(cond)) {                                      \
            std::printf("    FAIL: " __VA_ARGS__);          \
            std::printf("\n");                              \
            g_failures++;                                   \
            return false;                                   \
        }                                                   \
    } while (0)

struct Fired {
    uint64_t tick;
    uint64_t clockFrame; // value of the macro's clock when it fired
    ButtonEvent ev;
};

// Minimal stand-in for GJBaseGameLayer's per-tick loop.
struct SimGame {
    double tps;
    FrameClock clock;
    uint64_t tick = 0;      // physics step counter (m_currentProgress)
    double levelTime = 0.0; // accumulated like GD's m_levelTime

    uint64_t macroFrame() const {
        return currentMacroFrame(clock, static_cast<uint32_t>(tick), levelTime, tps);
    }
    void step() {
        tick++;
        levelTime += 1.0 / tps;
    }
};

static std::vector<Fired> runPlayback(Macro const& m, Playback& pb, bool flip, uint64_t stopAtTick, bool& stoppedEarly) {
    std::vector<Fired> log;
    SimGame g{m.framerate, m.clock};
    uint64_t lastNeeded = std::numeric_limits<uint64_t>::max();
    stoppedEarly = false;
    for (uint64_t guard = 0; guard < 50'000'000'000ull; guard++) {
        if (g.tick >= stopAtTick) { stoppedEarly = true; break; }
        auto sink = [&](ButtonEvent const& ev) { log.push_back({g.tick, g.macroFrame(), ev}); };
        pb.advance(g.macroFrame(), flip, sink);
        // half-tick: GD can call processCommands twice for one step; the frame
        // counter hasn't moved so nothing may fire twice
        if (g.tick % 3 == 1) pb.advance(g.macroFrame(), flip, sink);
        if (pb.exhausted()) break;
        // skip idle stretches quickly (frames can be in the billions)
        uint64_t nextFrame = m.inputs[pb.index()].frame;
        if (m.clock == FrameClock::PhysicsStep && nextFrame > g.tick + 2 && nextFrame - 1 < stopAtTick) {
            g.tick = nextFrame - 1;
            g.levelTime = static_cast<double>(g.tick) / g.tps;
        } else {
            g.step();
        }
        (void)lastNeeded;
    }
    return log;
}

// First tick at which the game's macro clock reaches `frame`.
static std::vector<uint64_t> expectedTicks(Macro const& m) {
    std::vector<uint64_t> out;
    out.reserve(m.inputs.size());
    if (m.clock == FrameClock::PhysicsStep) {
        for (auto const& in : m.inputs) out.push_back(in.frame);
        return out;
    }
    SimGame g{m.framerate, m.clock};
    for (auto const& in : m.inputs) {
        while (g.macroFrame() < in.frame) g.step();
        out.push_back(g.tick);
    }
    return out;
}

static bool readExpected(fs::path const& p, double& fr, int& plat, std::string& bot, std::vector<Input>& inputs) {
    std::ifstream f(p);
    if (!f) return false;
    std::string key;
    size_t count = 0;
    f >> key >> fr >> key >> plat >> key;
    std::getline(f, bot);
    if (!bot.empty() && bot[0] == ' ') bot.erase(0, 1);
    f >> key >> count;
    inputs.clear();
    for (size_t k = 0; k < count; k++) {
        uint64_t frame; int btn, p2, down;
        f >> frame >> btn >> p2 >> down;
        inputs.push_back({frame, static_cast<uint8_t>(btn), p2 != 0, down != 0});
    }
    return static_cast<bool>(f);
}

static bool sameInput(Input const& a, Input const& b) {
    return a.frame == b.frame && a.button == b.button && a.player2 == b.player2 && a.down == b.down;
}

static bool testFile(fs::path const& path) {
    std::printf("%s\n", path.filename().string().c_str());
    auto res = parseMacroFile(path);
    if (auto err = std::get_if<ParseError>(&res)) {
        std::printf("    FAIL: parse error: %s\n", err->message.c_str());
        g_failures++;
        return false;
    }
    auto macro = std::make_shared<Macro>(std::get<Macro>(std::move(res)));
    Macro const& m = *macro;
    std::printf("    %s, bot=%s, %zu inputs, %.0f tps, %s clock, last frame %llu%s\n",
        formatName(m.format), m.botName.c_str(), m.inputs.size(), m.framerate,
        m.clock == FrameClock::PhysicsStep ? "physics-step" : "xdBot level-time",
        static_cast<unsigned long long>(m.lastFrame()), m.platformer ? ", platformer" : "");

    // ---- 2. reference comparison ------------------------------------------
    auto expPath = path; expPath.replace_extension(".expected");
    if (fs::exists(expPath)) {
        double fr; int plat; std::string bot; std::vector<Input> exp;
        CHECK(readExpected(expPath, fr, plat, bot, exp), "could not read %s", expPath.string().c_str());
        bool refSorted = std::is_sorted(exp.begin(), exp.end(), [](auto& a, auto& b) { return a.frame < b.frame; });
        std::stable_sort(exp.begin(), exp.end(), [](auto& a, auto& b) { return a.frame < b.frame; });
        CHECK(exp.size() == m.inputs.size(), "input count %zu != reference %zu", m.inputs.size(), exp.size());
        for (size_t k = 0; k < exp.size(); k++) {
            CHECK(sameInput(exp[k], m.inputs[k]),
                "input #%zu differs from reference: ours f=%llu b=%d p2=%d d=%d, ref f=%llu b=%d p2=%d d=%d", k,
                (unsigned long long)m.inputs[k].frame, m.inputs[k].button, m.inputs[k].player2, m.inputs[k].down,
                (unsigned long long)exp[k].frame, exp[k].button, exp[k].player2, exp[k].down);
        }
        CHECK(fr == m.framerate, "framerate %f != reference %f", m.framerate, fr);
        CHECK(bot == m.botName, "bot name '%s' != reference '%s'", m.botName.c_str(), bot.c_str());
        if (plat >= 0) CHECK((plat != 0) == m.platformer, "platformer flag differs from reference");
        std::printf("    reference: all %zu inputs identical%s\n", exp.size(), refSorted ? "" : " (reference unsorted)");
    }

    // ---- 3. playback simulation, both settings of "flip 2P controls" -------
    for (bool flip : {false, true}) {
        Playback pb;
        pb.load(macro);
        bool early = false;
        auto log = runPlayback(m, pb, flip, std::numeric_limits<uint64_t>::max(), early);
        auto ticks = expectedTicks(m);

        CHECK(log.size() == m.inputs.size(), "fired %zu events for %zu inputs", log.size(), m.inputs.size());
        size_t exactTick = 0;
        for (size_t k = 0; k < log.size(); k++) {
            auto const& in = m.inputs[k];
            auto const& f = log[k];
            CHECK(f.ev.down == in.down && f.ev.button == in.button && f.ev.isPlayer1 == toIsPlayer1(in, m.playerFlag, flip),
                "event #%zu has wrong button/state/player", k);
            CHECK(f.tick == ticks[k], "event #%zu fired on tick %llu, expected %llu", k,
                (unsigned long long)f.tick, (unsigned long long)ticks[k]);
            if (k > 0) CHECK(log[k - 1].tick <= f.tick, "event #%zu fired before event #%zu", k, k - 1);
            // fired while the game clock reads exactly the recorded frame
            if (f.clockFrame == in.frame) exactTick++;
        }

        // holds: pair each press with its release per (player, button)
        std::map<std::pair<bool, int>, size_t> openPress;
        size_t holds = 0, holdMismatch = 0, tickHoldDiff = 0;
        for (size_t k = 0; k < log.size(); k++) {
            auto key = std::make_pair(log[k].ev.isPlayer1, log[k].ev.button);
            if (log[k].ev.down) {
                openPress[key] = k;
            } else if (auto it = openPress.find(key); it != openPress.end()) {
                size_t p = it->second;
                uint64_t recorded = m.inputs[k].frame - m.inputs[p].frame;
                // measured on the macro's own clock (== ticks for physics-step macros)
                uint64_t played = log[k].clockFrame - log[p].clockFrame;
                if (log[k].tick - log[p].tick != recorded) tickHoldDiff++;
                holds++;
                if (recorded != played) {
                    holdMismatch++;
                    if (holdMismatch <= 3)
                        std::printf("    hold #%zu: recorded %llu frames, played %llu ticks\n", holds,
                            (unsigned long long)recorded, (unsigned long long)played);
                        // (printed only on mismatch)
                }
                openPress.erase(it);
            }
        }
        CHECK(holdMismatch == 0, "%zu of %zu holds had a different duration", holdMismatch, holds);

        // simultaneous inputs / overlapping holds (reported, all verified above)
        size_t sameTick = 0, overlapTicks = 0;
        std::map<std::pair<bool, int>, bool> held;
        for (size_t k = 0; k < log.size(); k++) {
            if (k > 0 && log[k].tick == log[k - 1].tick) sameTick++;
            held[{log[k].ev.isPlayer1, log[k].ev.button}] = log[k].ev.down;
            size_t n = 0;
            for (auto& [kk, h] : held) n += h;
            if (n >= 2) overlapTicks++;
        }

        if (!flip) {
            std::printf("    playback: %zu/%zu events fired at their exact recorded frame, %zu/%zu holds exact, "
                        "%zu same-tick events, %zu overlapping-hold transitions\n",
                exactTick, log.size(), holds - holdMismatch, holds, sameTick, overlapTicks);
            if (m.clock == FrameClock::XdBotLevelTime)
                std::printf("    (xdBot clock: %zu holds differ by a tick from their frame count, same as in xdBot itself)\n", tickHoldDiff);
        }
        CHECK(exactTick == log.size(), "%zu events not at their recorded frame", log.size() - exactTick);
    }

    // ---- 4. death mid-run: stop, release, nothing fires afterwards ---------
    {
        Playback pb;
        pb.load(macro);
        auto ticks = expectedTicks(m);
        uint64_t deathTick = ticks[ticks.size() / 2] + (ticks.back() > ticks.front() ? 1 : 0);
        bool early = false;
        auto log = runPlayback(m, pb, false, deathTick, early);
        for (auto const& f : log) CHECK(f.tick < deathTick, "input fired at/after death tick");
        // what the recording holds at that moment
        std::map<std::pair<bool, int>, bool> held;
        for (auto const& f : log) held[{f.ev.isPlayer1, f.ev.button}] = f.ev.down;
        std::vector<ButtonEvent> released;
        pb.releaseHeld([&](ButtonEvent const& ev) { released.push_back(ev); });
        size_t expectRel = 0;
        for (auto& [k, h] : held) {
            if (!h) continue;
            expectRel++;
            bool found = std::any_of(released.begin(), released.end(), [&](auto& e) {
                return !e.down && e.isPlayer1 == k.first && e.button == k.second;
            });
            CHECK(found, "held button not released on stop");
        }
        CHECK(released.size() == expectRel, "released %zu buttons, %zu were held", released.size(), expectRel);
        CHECK(!pb.anyHeld(), "buttons still tracked as held after stop");
        if (early && !pb.exhausted()) {
            // nothing more may fire once stopped: the game layer stops calling advance
        }

        // ---- restart: replays identically from the start ------------------
        pb.restart();
        bool e2 = false;
        auto again = runPlayback(m, pb, false, std::numeric_limits<uint64_t>::max(), e2);
        Playback fresh;
        fresh.load(macro);
        bool e3 = false;
        auto ref = runPlayback(m, fresh, false, std::numeric_limits<uint64_t>::max(), e3);
        CHECK(again.size() == ref.size(), "restart replay differs in length");
        for (size_t k = 0; k < ref.size(); k++)
            CHECK(again[k].tick == ref[k].tick && again[k].ev.down == ref[k].ev.down &&
                  again[k].ev.button == ref[k].ev.button && again[k].ev.isPlayer1 == ref[k].ev.isPlayer1,
                  "restart replay differs at event %zu", k);
        std::printf("    death at tick %llu: %zu fired before, %zu held buttons released; restart replays identically\n",
            (unsigned long long)deathTick, log.size(), released.size());
    }

    if (m.unmatchedPresses || m.unmatchedReleases || m.repeatedPresses || m.hadToSort)
        std::printf("    note: %zu unreleased presses, %zu releases while up, %zu repeat presses%s (played as recorded)\n",
            m.unmatchedPresses, m.unmatchedReleases, m.repeatedPresses, m.hadToSort ? ", file was out of order" : "");
    std::printf("    PASS\n");
    return true;
}

static bool expectParseError(char const* name, std::vector<uint8_t> data) {
    auto res = parseMacro(data);
    if (std::holds_alternative<ParseError>(res)) {
        std::printf("  rejects %-28s -> %s\n", name, std::get<ParseError>(res).message.c_str());
        return true;
    }
    std::printf("  FAIL: accepted %s\n", name);
    g_failures++;
    return false;
}

int main(int argc, char** argv) {
    std::vector<fs::path> files;
    for (int k = 1; k < argc; k++) {
        fs::path p = argv[k];
        if (fs::is_directory(p)) {
            for (auto const& e : fs::directory_iterator(p)) {
                auto ext = e.path().extension().string();
                if (ext == ".gdr" || ext == ".gdr2" || ext == ".json") files.push_back(e.path());
            }
        } else {
            files.push_back(p);
        }
    }
    std::sort(files.begin(), files.end());

    size_t passed = 0;
    for (auto const& f : files) passed += testFile(f);

    std::printf("\nmalformed input handling:\n");
    expectParseError("empty file", {});
    expectParseError("truncated GDR2", {'G', 'D', 'R', 2, 0});
    expectParseError("GDR2 bad input count", {'G', 'D', 'R', 2, 0, 0, 0, 0, 0, 0, 0, 0, 0x40, 0x6E, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x7F, 0});
    expectParseError("truncated msgpack", {0x81, 0xa6, 'i', 'n', 'p'});
    std::string noInputs = R"({"framerate":240,"inputs":[]})";
    expectParseError("JSON without inputs", {noInputs.begin(), noInputs.end()});
    std::string fractional = R"({"inputs":[{"frame":10.5,"btn":1,"2p":false,"down":true}]})";
    expectParseError("JSON fractional frame", {fractional.begin(), fractional.end()});
    std::string badBtn = R"({"inputs":[{"frame":10,"btn":7,"2p":false,"down":true}]})";
    expectParseError("JSON unknown button", {badBtn.begin(), badBtn.end()});
    std::string deep;
    for (int k = 0; k < 200; k++) deep += "{\"a\":";
    expectParseError("deeply nested JSON", {deep.begin(), deep.end()});

    std::printf("\n%zu/%zu macro files passed, %d failure(s)\n", passed, files.size(), g_failures);
    return g_failures == 0 ? 0 : 1;
}
