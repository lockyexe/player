#pragma once

#include "Macro.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>

namespace mp {

// One button event, already translated to GJBaseGameLayer::handleButton args.
struct ButtonEvent {
    bool down;
    int button;     // 1 jump, 2 left, 3 right
    bool isPlayer1; // handleButton's third argument
};

// Frame number the macro's clock reports for the current game state.
// Mirrors how each recorder wrote its frames, so a recorded frame N is replayed
// at the same game moment it was captured.
inline uint64_t currentMacroFrame(FrameClock clock, uint32_t physicsStep, double levelTime, double framerate) {
    switch (clock) {
        case FrameClock::XdBotLevelTime: {
            double f = levelTime * framerate;
            if (!(f >= 0.0)) return 0;
            return static_cast<uint64_t>(static_cast<int64_t>(f)) + 1;
        }
        case FrameClock::PhysicsStep:
        default:
            return physicsStep;
    }
}

inline bool toIsPlayer1(Input const& in, PlayerFlag flag, bool flipControls) {
    switch (flag) {
        case PlayerFlag::XdBotRaw: return in.player2 != flipControls;
        case PlayerFlag::IsPlayer2:
        default: return !in.player2;
    }
}

// Walks a macro's inputs in order against an advancing frame counter.
// - every input fires exactly once, in file order (no skipping, no reordering)
// - an input fires on the first tick whose frame is >= its recorded frame
// - holds last until the recorded release fires
class Playback {
public:
    void load(std::shared_ptr<Macro const> macro) {
        m_macro = std::move(macro);
        restart();
    }
    void restart() {
        m_index = 0;
        m_held = {};
    }
    std::shared_ptr<Macro const> const& macro() const { return m_macro; }
    size_t index() const { return m_index; }
    size_t total() const { return m_macro ? m_macro->inputs.size() : 0; }
    bool exhausted() const { return !m_macro || m_index >= m_macro->inputs.size(); }

    bool anyHeld() const {
        for (auto const& p : m_held)
            for (bool h : p)
                if (h) return true;
        return false;
    }

    // Fires every input due at `frame`. Returns how many fired.
    template <class Sink>
    size_t advance(uint64_t frame, bool flipControls, Sink&& sink) {
        if (!m_macro) return 0;
        auto const& inputs = m_macro->inputs;
        size_t fired = 0;
        while (m_index < inputs.size() && inputs[m_index].frame <= frame) {
            auto const& in = inputs[m_index];
            ButtonEvent ev{in.down, in.button, toIsPlayer1(in, m_macro->playerFlag, flipControls)};
            setHeld(ev);
            m_index++;
            fired++;
            sink(ev);
        }
        return fired;
    }

    // Releases whatever the macro is still holding (used when stopping).
    template <class Sink>
    void releaseHeld(Sink&& sink) {
        for (int p = 0; p < 2; p++) {
            for (int b = 1; b <= 3; b++) {
                if (m_held[p][b]) {
                    m_held[p][b] = false;
                    sink(ButtonEvent{false, b, p == 0});
                }
            }
        }
    }
    void forgetHeld() { m_held = {}; }

private:
    void setHeld(ButtonEvent const& ev) {
        if (ev.button < 1 || ev.button > 3) return;
        m_held[ev.isPlayer1 ? 0 : 1][ev.button] = ev.down;
    }

    std::shared_ptr<Macro const> m_macro;
    size_t m_index = 0;
    std::array<std::array<bool, 4>, 2> m_held{}; // [player1?0:1][button]
};

} // namespace mp
