// Macro Player: replays .gdr macros input-for-input.
//
// Timing model (same as the GDR reference bot and xdBot):
//   after GD runs a physics step's commands, every macro input whose recorded
//   frame has been reached is fed to GJBaseGameLayer::handleButton, in file
//   order. One processCommands call == one physics tick, so presses, holds and
//   releases land on the tick they were recorded on.

#include "Controller.hpp"
#include "MenuPopup.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/utils/Keyboard.hpp>

using namespace geode::prelude;

class $modify(MPBaseGameLayer, GJBaseGameLayer) {
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        auto pl = PlayLayer::get();
        if (pl && static_cast<GJBaseGameLayer*>(pl) == this) {
            Controller::get().onTick(pl);
        }
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        // While a macro is playing, it is the only source of input.
        if (Controller::get().shouldBlockInput(this)) return;
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
};

class $modify(MPPlayLayer, PlayLayer) {
    void resetLevel() {
        PlayLayer::resetLevel();
        Controller::get().onAttemptStart(this);
    }

    void startGame() {
        PlayLayer::startGame();
        Controller::get().onAttemptStart(this);
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        PlayLayer::destroyPlayer(player, object);
        Controller::get().onPlayerDestroyed(this, player);
    }

    void levelComplete() {
        Controller::get().onLevelComplete(this);
        PlayLayer::levelComplete();
    }

    void onQuit() {
        Controller::get().onLevelExit(this);
        PlayLayer::onQuit();
    }
};

$execute {
    KeyboardInputEvent(KEY_CapsLock).listen([](KeyboardInputData& data) {
        if (data.action != KeyboardInputData::Action::Press) return ListenerResult::Propagate;
        MenuPopup::toggle();
        return ListenerResult::Stop;
    }).leak();
}
