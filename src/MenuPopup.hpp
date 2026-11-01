#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/utils/file.hpp>

// The only UI: a small popup toggled with Caps Lock. Nothing is drawn on
// screen while it is closed.
class MenuPopup : public geode::Popup {
public:
    static void toggle();

protected:
    static MenuPopup* create();
    bool initMenu();
    ~MenuPopup() override;
    void onClose(cocos2d::CCObject*) override;
    void update(float dt) override;

    void refresh();
    void cycle(int direction);
    void onImport();
    void onFolder();
    void onPlayStop();
    void importFile(std::filesystem::path const& source);
    void setPlayButton(bool playing);

    cocos2d::CCLabelBMFont* m_nameLabel = nullptr;
    cocos2d::CCLabelBMFont* m_infoLabel = nullptr;
    cocos2d::CCLabelBMFont* m_noteLabel = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    CCMenuItemSpriteExtra* m_playButton = nullptr;
    bool m_showingStop = false;

    geode::async::TaskHolder<geode::utils::file::PickResult> m_pick;

    static MenuPopup* s_open;
};
