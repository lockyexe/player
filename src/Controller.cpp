#include "Controller.hpp"

#include "core/MacroParser.hpp"

#include <algorithm>

using namespace geode::prelude;

Controller& Controller::get() {
    static Controller instance;
    return instance;
}

// ---------------------------------------------------------------------------
// macro library
// ---------------------------------------------------------------------------

std::filesystem::path Controller::macrosDir() const {
    auto dir = Mod::get()->getSaveDir() / "macros";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::vector<std::filesystem::path> Controller::listMacros() const {
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    for (auto const& entry : std::filesystem::directory_iterator(macrosDir(), ec)) {
        if (!entry.is_regular_file(ec)) continue;
        auto ext = geode::utils::string::toLower(geode::utils::string::pathToString(entry.path().extension()));
        if (ext == ".gdr" || ext == ".gdr2" || ext == ".json") out.push_back(entry.path());
    }
    std::sort(out.begin(), out.end(), [](auto const& a, auto const& b) {
        return geode::utils::string::toLower(geode::utils::string::pathToString(a.filename())) <
               geode::utils::string::toLower(geode::utils::string::pathToString(b.filename()));
    });
    return out;
}

bool Controller::load(std::filesystem::path const& path) {
    if (m_state == State::Playing || m_state == State::Finished || m_state == State::Armed) {
        this->stop();
    }
    auto res = mp::parseMacroFile(path);
    if (auto err = std::get_if<mp::ParseError>(&res)) {
        m_loadError = err->message;
        m_macro.reset();
        m_macroPath = path;
        m_playback.load(nullptr);
        log::warn("Could not load macro {}: {}", geode::utils::string::pathToString(path), err->message);
        return false;
    }
    auto macro = std::make_shared<mp::Macro const>(std::get<mp::Macro>(std::move(res)));
    log::info("Loaded {} ({}, {} inputs, {} tps, bot '{}')",
        geode::utils::string::pathToString(path.filename()), mp::formatName(macro->format),
        macro->inputs.size(), macro->framerate, macro->botName);
    m_macro = std::move(macro);
    m_macroPath = path;
    m_loadError.clear();
    m_playback.load(m_macro);
    m_status = "Idle";
    Mod::get()->setSavedValue<std::string>("selected-macro",
        geode::utils::string::pathToString(path.filename()));
    return true;
}

void Controller::loadSavedSelection() {
    if (m_macro || !m_macroPath.empty()) return;
    auto name = Mod::get()->getSavedValue<std::string>("selected-macro", "");
    if (!name.empty()) {
        auto path = macrosDir() / name;
        std::error_code ec;
        if (std::filesystem::exists(path, ec)) {
            this->load(path);
            return;
        }
    }
    auto all = listMacros();
    if (!all.empty()) this->load(all.front());
}

// ---------------------------------------------------------------------------
// playback control
// ---------------------------------------------------------------------------

void Controller::play() {
    if (!m_macro) return;
    m_playback.restart();
    m_state = State::Armed;
    m_status = "Waiting for an attempt";
    if (auto pl = PlayLayer::get()) {
        // A run always starts from the very first tick of an attempt.
        if (pl->m_isPracticeMode) pl->resetLevelFromStart();
        else pl->resetLevel();
    }
}

void Controller::stop() {
    if (m_state == State::Idle) return;
    this->end(PlayLayer::get(), State::Idle, "Stopped", true);
}

void Controller::begin(PlayLayer* pl) {
    m_playback.restart();
    m_state = State::Playing;
    m_status = "Playing";
    log::info("Playback started on attempt (tick {})", pl->m_gameState.m_currentProgress);
}

void Controller::end(PlayLayer* pl, State next, std::string status, bool releaseHeld) {
    if (releaseHeld && pl && pl == PlayLayer::get()) {
        m_playback.releaseHeld([&](mp::ButtonEvent const& ev) { this->inject(pl, ev); });
    } else {
        m_playback.forgetHeld();
    }
    m_state = next;
    m_status = std::move(status);
    log::info("Playback ended: {} ({}/{} inputs)", m_status, m_playback.index(), m_playback.total());
}

void Controller::inject(PlayLayer* pl, mp::ButtonEvent const& ev) {
    m_injecting = true;
    pl->handleButton(ev.down, ev.button, ev.isPlayer1);
    m_injecting = false;
}

uint64_t Controller::currentFrame(PlayLayer* pl) const {
    return mp::currentMacroFrame(m_macro->clock, pl->m_gameState.m_currentProgress,
        pl->m_gameState.m_levelTime, m_macro->framerate);
}

bool Controller::flipControls(PlayLayer* pl) const {
    // Only matters for xdBot files, which store the raw (possibly flipped)
    // player argument; xdBot ignores the option in platformer levels.
    if (m_macro->playerFlag != mp::PlayerFlag::XdBotRaw) return false;
    if (pl->m_levelSettings && pl->m_levelSettings->m_platformerMode) return false;
    return GameManager::get()->getGameVariable("0010");
}

// ---------------------------------------------------------------------------
// hooks
// ---------------------------------------------------------------------------

void Controller::onAttemptStart(PlayLayer* pl) {
    if (!m_macro || m_state == State::Idle) return;
    bool fromStart = pl->m_gameState.m_currentProgress == 0;

    if (m_state == State::Armed) {
        if (fromStart) this->begin(pl);
        else m_status = "Waiting for an attempt from the start";
        return;
    }
    // The level was restarted. GD has already reset the players, so only our
    // hold tracking needs dropping.
    m_playback.forgetHeld();
    if (m_state == State::Finished) {
        this->end(pl, State::Idle, "Finished", false);
    } else if (fromStart) {
        this->begin(pl); // restarted mid-run: replay from the first input
    } else {
        this->end(pl, State::Idle, "Stopped: restarted from a checkpoint", false);
    }
}

void Controller::onTick(PlayLayer* pl) {
    if (m_state != State::Playing && m_state != State::Finished) return;
    if (!m_macro) return;

    if (pl->m_player1 && pl->m_player1->m_isDead) {
        this->end(pl, State::Idle, fmt::format("Stopped: died ({}/{} inputs)", m_playback.index(), m_playback.total()), true);
        return;
    }
    if (m_state == State::Finished) {
        if (!m_playback.anyHeld()) this->end(pl, State::Idle, "Finished", false);
        return;
    }

    uint64_t frame = this->currentFrame(pl);
    bool flip = this->flipControls(pl);
    m_playback.advance(frame, flip, [&](mp::ButtonEvent const& ev) { this->inject(pl, ev); });

    if (m_playback.exhausted()) {
        if (m_playback.anyHeld()) {
            // The recording ends with buttons still down: keep them down (as
            // recorded) until the attempt ends, but hand control back.
            m_state = State::Finished;
            m_status = "Finished (macro ended holding)";
        } else {
            this->end(pl, State::Idle, "Finished", false);
        }
    }
}

void Controller::onPlayerDestroyed(PlayLayer* pl, PlayerObject* player) {
    if (m_state != State::Playing && m_state != State::Finished) return;
    if (!player || !player->m_isDead) return; // noclip, anticheat spike, etc.
    this->end(pl, State::Idle, fmt::format("Stopped: died ({}/{} inputs)", m_playback.index(), m_playback.total()), true);
}

void Controller::onLevelComplete(PlayLayer* pl) {
    if (m_state != State::Playing && m_state != State::Finished) return;
    this->end(pl, State::Idle, fmt::format("Finished: level complete ({}/{} inputs)", m_playback.index(), m_playback.total()), false);
}

void Controller::onLevelExit(PlayLayer*) {
    if (m_state != State::Playing && m_state != State::Finished) return;
    this->end(nullptr, State::Idle, "Stopped: left the level", false);
}

bool Controller::shouldBlockInput(GJBaseGameLayer* layer) const {
    return m_state == State::Playing && !m_injecting && layer == PlayLayer::get();
}
