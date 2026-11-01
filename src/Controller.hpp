#pragma once

#include "core/Macro.hpp"
#include "core/Playback.hpp"

#include <Geode/Geode.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// Owns the loaded macro and the playback state, and is driven by the game
// hooks in main.cpp. All methods run on the main thread.
class Controller {
public:
    enum class State {
        Idle,     // nothing playing
        Armed,    // waiting for the next attempt to start from the beginning
        Playing,  // feeding inputs; real player input is blocked
        Finished, // all inputs fed; still tracking holds the macro left down
    };

    static Controller& get();

    // ---- macro library ---------------------------------------------------
    std::filesystem::path macrosDir() const;
    std::vector<std::filesystem::path> listMacros() const;
    bool load(std::filesystem::path const& path);
    void loadSavedSelection();
    std::shared_ptr<mp::Macro const> const& macro() const { return m_macro; }
    std::filesystem::path const& macroPath() const { return m_macroPath; }
    std::string const& loadError() const { return m_loadError; }

    // ---- playback ----------------------------------------------------------
    State state() const { return m_state; }
    std::string const& status() const { return m_status; }
    size_t played() const { return m_playback.index(); }
    void play();
    void stop();

    // ---- game hooks --------------------------------------------------------
    void onAttemptStart(PlayLayer* pl);
    void onTick(PlayLayer* pl);
    void onPlayerDestroyed(PlayLayer* pl, PlayerObject* player);
    void onLevelComplete(PlayLayer* pl);
    void onLevelExit(PlayLayer* pl);
    bool shouldBlockInput(GJBaseGameLayer* layer) const;

private:
    Controller() = default;

    void begin(PlayLayer* pl);
    void end(PlayLayer* pl, State next, std::string status, bool releaseHeld);
    void inject(PlayLayer* pl, mp::ButtonEvent const& ev);
    uint64_t currentFrame(PlayLayer* pl) const;
    bool flipControls(PlayLayer* pl) const;

    std::shared_ptr<mp::Macro const> m_macro;
    std::filesystem::path m_macroPath;
    std::string m_loadError;

    mp::Playback m_playback;
    State m_state = State::Idle;
    std::string m_status = "Idle";
    bool m_injecting = false;
};
