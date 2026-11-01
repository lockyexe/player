#include "MenuPopup.hpp"

#include "Controller.hpp"
#include "core/MacroParser.hpp"

#include <Geode/utils/cocos.hpp>

#include <fstream>
#include <iterator>

using namespace geode::prelude;

MenuPopup* MenuPopup::s_open = nullptr;

void MenuPopup::toggle() {
    if (s_open) {
        s_open->onClose(nullptr);
        return;
    }
    if (!CCDirector::get()->getRunningScene()) return;
    if (auto popup = MenuPopup::create()) popup->show();
}

MenuPopup* MenuPopup::create() {
    auto ret = new MenuPopup();
    if (ret->initMenu()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

MenuPopup::~MenuPopup() {
    if (s_open == this) s_open = nullptr;
}

void MenuPopup::onClose(CCObject* sender) {
    if (s_open == this) s_open = nullptr;
    Popup::onClose(sender);
}

bool MenuPopup::initMenu() {
    if (!Popup::init(300.f, 215.f)) return false;
    s_open = this;
    this->setTitle("Macro Player");

    m_nameLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_mainLayer->addChildAtPosition(m_nameLabel, Anchor::Top, ccp(0, -50));

    m_infoLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_infoLabel->setAlignment(kCCTextAlignmentCenter);
    m_mainLayer->addChildAtPosition(m_infoLabel, Anchor::Top, ccp(0, -80));

    m_noteLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_noteLabel->setAlignment(kCCTextAlignmentCenter);
    m_mainLayer->addChildAtPosition(m_noteLabel, Anchor::Top, ccp(0, -108));

    m_statusLabel = CCLabelBMFont::create("", "goldFont.fnt");
    m_mainLayer->addChildAtPosition(m_statusLabel, Anchor::Center, ccp(0, -27));

    // < Import Folder >
    auto nav = CCMenu::create();
    nav->setContentSize({260.f, 36.f});

    auto prevSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
    prevSpr->setScale(.55f);
    nav->addChild(CCMenuItemExt::createSpriteExtra(prevSpr, [this](auto) { this->cycle(-1); }));

    auto importSpr = ButtonSprite::create("Import", "bigFont.fnt", "GJ_button_04.png", .55f);
    importSpr->setScale(.75f);
    nav->addChild(CCMenuItemExt::createSpriteExtra(importSpr, [this](auto) { this->onImport(); }));

    auto folderSpr = ButtonSprite::create("Folder", "bigFont.fnt", "GJ_button_04.png", .55f);
    folderSpr->setScale(.75f);
    nav->addChild(CCMenuItemExt::createSpriteExtra(folderSpr, [this](auto) { this->onFolder(); }));

    auto nextSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
    nextSpr->setScale(.55f);
    nextSpr->setFlipX(true);
    nav->addChild(CCMenuItemExt::createSpriteExtra(nextSpr, [this](auto) { this->cycle(1); }));

    nav->setLayout(RowLayout::create()->setGap(12.f));
    m_mainLayer->addChildAtPosition(nav, Anchor::Bottom, ccp(0, 70));

    auto playMenu = CCMenu::create();
    playMenu->setContentSize({120.f, 40.f});
    m_playButton = CCMenuItemExt::createSpriteExtra(
        ButtonSprite::create("Play", "goldFont.fnt", "GJ_button_01.png", .9f),
        [this](auto) { this->onPlayStop(); });
    playMenu->addChildAtPosition(m_playButton, Anchor::Center);
    m_mainLayer->addChildAtPosition(playMenu, Anchor::Bottom, ccp(0, 30));

    Controller::get().loadSavedSelection();
    this->refresh();
    this->scheduleUpdate();
    return true;
}

void MenuPopup::setPlayButton(bool playing) {
    if (!m_playButton || playing == m_showingStop) return;
    m_showingStop = playing;
    auto spr = playing
        ? ButtonSprite::create("Stop", "goldFont.fnt", "GJ_button_06.png", .9f)
        : ButtonSprite::create("Play", "goldFont.fnt", "GJ_button_01.png", .9f);
    m_playButton->setNormalImage(spr);
    m_playButton->setContentSize(spr->getScaledContentSize());
    spr->setPosition(spr->getScaledContentSize() / 2);
}

void MenuPopup::update(float) {
    auto& c = Controller::get();
    std::string status;
    switch (c.state()) {
        case Controller::State::Playing:
            status = fmt::format("Playing  {}/{}", c.played(), c.macro() ? c.macro()->inputs.size() : 0);
            break;
        default:
            status = c.status();
            break;
    }
    if (m_statusLabel->getString() != status) {
        m_statusLabel->setString(status.c_str());
        m_statusLabel->limitLabelWidth(260.f, .5f, .2f);
    }
    bool active = c.state() == Controller::State::Playing || c.state() == Controller::State::Armed ||
                  c.state() == Controller::State::Finished;
    this->setPlayButton(active);
}

void MenuPopup::refresh() {
    auto& c = Controller::get();
    auto const& macro = c.macro();

    std::string name, info, note;
    ccColor3B noteColor = {255, 170, 60};

    if (c.macroPath().empty()) {
        name = "No macro";
        info = "Import a .gdr / .gdr2 / .json macro";
    } else {
        name = geode::utils::string::pathToString(c.macroPath().stem());
        if (!macro) {
            info = "Could not read this file";
            note = c.loadError();
            noteColor = {255, 90, 90};
        } else {
            info = fmt::format("{} inputs  |  {:g} TPS  |  {}\n{}{}{}",
                macro->inputs.size(), macro->framerate, mp::formatName(macro->format),
                macro->botName.empty() ? "unknown bot" : macro->botName,
                macro->levelName.empty() ? "" : "  |  ",
                macro->levelName);
            if (macro->framerate != 240.0)
                note = fmt::format("Recorded at {:g} TPS: the game must run at {:g} TPS", macro->framerate, macro->framerate);
        }
    }

    m_nameLabel->setString(name.c_str());
    m_nameLabel->limitLabelWidth(250.f, .55f, .15f);
    m_infoLabel->setString(info.c_str());
    m_infoLabel->limitLabelWidth(270.f, .75f, .2f);
    m_noteLabel->setString(note.c_str());
    m_noteLabel->setColor(noteColor);
    m_noteLabel->limitLabelWidth(270.f, .7f, .2f);
    this->update(0.f);
}

void MenuPopup::cycle(int direction) {
    auto& c = Controller::get();
    auto all = c.listMacros();
    if (all.empty()) return;
    long idx = -1;
    for (size_t k = 0; k < all.size(); k++)
        if (all[k] == c.macroPath()) idx = static_cast<long>(k);
    long n = static_cast<long>(all.size());
    long next = idx < 0 ? 0 : ((idx + direction) % n + n) % n;
    c.load(all[static_cast<size_t>(next)]);
    this->refresh();
}

void MenuPopup::onFolder() {
    file::openFolder(Controller::get().macrosDir());
}

void MenuPopup::onPlayStop() {
    auto& c = Controller::get();
    if (c.state() == Controller::State::Idle) {
        if (!c.macro()) return;
        c.play();
        // get out of the way so the attempt can be watched
        this->onClose(nullptr);
    } else {
        c.stop();
        this->refresh();
    }
}

void MenuPopup::onImport() {
    m_pick.spawn(
        file::pick(file::PickMode::OpenFile, file::FilePickOptions {
            .defaultPath = std::nullopt,
            .filters = { file::FilePickOptions::Filter {
                .description = "GD macros",
                .files = { "*.gdr", "*.gdr2", "*.json" },
            }}
        }),
        [this](file::PickResult result) {
            if (result.isErr()) {
                FLAlertLayer::create("Import", result.unwrapErr(), "OK")->show();
                return;
            }
            auto picked = std::move(result).unwrap();
            if (picked) this->importFile(*picked);
        });
}

static bool sameContents(std::filesystem::path const& a, std::filesystem::path const& b) {
    std::error_code ec;
    if (std::filesystem::file_size(a, ec) != std::filesystem::file_size(b, ec) || ec) return false;
    std::ifstream fa(a, std::ios::binary), fb(b, std::ios::binary);
    return std::equal(std::istreambuf_iterator<char>(fa), std::istreambuf_iterator<char>(),
                      std::istreambuf_iterator<char>(fb));
}

void MenuPopup::importFile(std::filesystem::path const& source) {
    auto& c = Controller::get();
    auto dir = c.macrosDir();
    std::error_code ec;

    // Validate before copying so a broken file never lands in the library.
    auto parsed = mp::parseMacroFile(source);
    if (auto err = std::get_if<mp::ParseError>(&parsed)) {
        FLAlertLayer::create("Import",
            fmt::format("<cr>Can't read this macro:</c>\n{}", err->message), "OK")->show();
        return;
    }

    auto dest = dir / source.filename();
    if (std::filesystem::exists(dest, ec) && !sameContents(source, dest)) {
        for (int n = 2; n < 1000; n++) {
            auto name = source.stem();
            name += fmt::format(" ({})", n); // ASCII only, safe for any path encoding
            name += source.extension();
            dest = dir / name;
            if (!std::filesystem::exists(dest, ec) || sameContents(source, dest)) break;
        }
    }
    if (!std::filesystem::exists(dest, ec)) {
        std::filesystem::copy_file(source, dest, ec);
        if (ec) {
            FLAlertLayer::create("Import", fmt::format("Could not copy the file:\n{}", ec.message()), "OK")->show();
            return;
        }
    }
    c.load(dest);
    this->refresh();
}
