#include <Geode/Geode.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/utils/web.hpp>

#include <zip_file.hpp>

uint32_t fnv1a_hash(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) return 0;

    uint32_t hash = 2166136261u;
    char c;
    while (file.get(c)) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 16777619u;
    }
    return hash;
}


using namespace geode::prelude; 

#include <regex>

static auto dark_themed = false;

inline CCTexture2D* createTextureFromPNGData(const std::vector<uint8_t>& pngData) {
    CCImage* image = new CCImage();
    bool success = image->initWithImageData((void*)pngData.data(), pngData.size(), CCImage::kFmtPng);
    if (!success) {
        log::error("Failed to create CCImage from PNG data.");
        image->release();
        return nullptr;
    }

    CCTexture2D* texture = new CCTexture2D();
    if (!texture->initWithImage(image)) {
        log::error("Failed to create CCTexture2D from CCImage.");
        texture->release();
        image->release();
        return nullptr;
    }

    image->release(); // Texture holds its own copy now
    return texture;
}

//aaaaaaaaaaaaaaaaaaaaaaaaaa
class CustomKeypadListener : public CCLayer {
public:
    CREATE_FUNC(CustomKeypadListener);
    bool init() override {
        if (!CCLayer::init()) return false;
        this->setKeypadEnabled(1);
        this->setTouchEnabled(0);
        this->setContentSize(CCSizeZero);
        return this;
    }
    std::function<void()> m_keyBackClicked = [] {};
    void keyBackClicked() override {
        if (m_keyBackClicked) m_keyBackClicked();
    }
};

class Modpack : public CCObject {
    void loadLogo(std::string link) {
        Ref loading_action = CCRepeatForever::create(CCSequence::create(
            CCFadeTo::create(0.3f, 120),
            CCFadeTo::create(0.3f, 210),
            nullptr
        ));
        if (logo) logo->runAction(loading_action);
        auto listener = new EventListener<web::WebTask>;
        listener->bind([this, loading_action, link](web::WebTask::Event* e)
            {
                if (web::WebResponse* res = e->getValue()) {
                    if (auto a = createTextureFromPNGData(res->data())) {
                        //apply texture
                        if (logo) {
                            if (loading_action) logo->stopAction(loading_action);
                            logo->setOpacity(255);
                            logo->initWithTexture(a);
                        }
                        //save frame
                        CCSpriteFrameCache::get()->addSpriteFrame(
                            CCSpriteFrame::createWithTexture(
                                a, { {0,0}, a->getContentSize() }
                            ), link.c_str()
                        );
                    };
                };
            }
        );
        auto req = web::WebRequest();
        listener->setFilter(req.get(link));
    }
public:
    std::filesystem::path path;
    matjson::Value data;
    std::string about;
    Ref<CCSprite> logo;

    bool include_settings_data = true;
    bool include_saved_data = false;

    bool include_config = true;
    bool include_saves = false;

    inline static auto packsLoadPoints = std::map<std::filesystem::path, uint32_t>{};
    inline static auto loadedPacks = std::map<std::filesystem::path, Ref<Modpack>>{};

    void loadFromFile(std::filesystem::path path) {
        this->path = CCFileUtils::get()->fullPathForFilename(path.string().c_str(), false);
        if (string::contains(path.string(), ".geode_modpack")) {
            auto size = packsLoadPoints.contains(path) ? packsLoadPoints[path] : 0;
            auto size_mismatch = size != fnv1a_hash(path.string());
            if (not size_mismatch) {
                auto loaded = loadedPacks[path].data();
                data = loaded->data;
                about = loaded->about;
                if (loaded->logo) logo->setDisplayFrame(loaded->logo->displayFrame());
                include_settings_data = loaded->include_settings_data;
                include_saved_data = loaded->include_saved_data;
                include_config = loaded->include_config;
                include_saves = loaded->include_saves;
            }
            else if (auto file_open = file::CCMiniZFile::create(path.string())) {
                auto file = file_open.unwrapOrDefault();

                if (auto read = file->read("this.geode_modlist")) {
                    data = matjson::parse(read.unwrapOrDefault()).unwrapOrDefault();
                } else log::error("failed to read this.geode_modlist, {}", read.err().value_or("unk err"));

                if (auto read = file->read("about.md")) {
                    about = read.unwrapOrDefault();
                }
                else log::warn("failed to read about.md, {}", read.err().value_or("unk err"));

                if (auto read = file->read("README.md")) {
                    about = read.unwrapOrDefault();
                }
                else log::info("failed to read README.md, {}", read.err().value_or("unk err"));

                if (auto read = file->readAsCCTexture("logo.png")) {
                    logo->initWithTexture(read.unwrapOrDefault());
                }
                else log::warn("failed to read logo.png, {}", read.err().value_or("unk err"));

                if (auto read = file->readAsCCTexture("pack.png")) {
                    logo->initWithTexture(read.unwrapOrDefault());
                }
                else log::info("failed to read pack.png, {}", read.err().value_or("unk err"));

                loadedPacks[path] = this;
                packsLoadPoints[path] = std::filesystem::file_size(path);
            }
        }
        else {
            auto read = file::readJson(path).unwrapOrDefault();
            data = read;
        };

        if (data.contains("logo")) {
            logo->initWithSpriteFrameName("geode.loader/logo-base.png");
            auto val = data["logo"].asString().unwrapOrDefault();

            static const std::regex link_regex(R"(^(https?|ftp)://[^\s/$.?#].[^\s]*$)", std::regex::icase);
            if (std::regex_match(val, link_regex)) {
                if (CCSpriteFrameCache::get()->m_pSpriteFrames->objectForKey(val.c_str())) {
                    logo->initWithSpriteFrameName(val.c_str());
                }
                else loadLogo(val);
            }
            else {
                if (fileExistsInSearchPaths(val.c_str())) logo->initWithFile(val.c_str());
                else logo->initWithSpriteFrameName(val.c_str());
            }
        }

        include_settings_data = string::contains(data.dump(), "\"settings\":") ? true : include_settings_data;
        include_saved_data = string::contains(data.dump(), "\"saved\":") ? true : include_saved_data;

        about = about.size() ? about : data["about"].asString().unwrapOr(
            "\n" "# " + data["name"].asString().unwrapOrDefault() +
            "\n" "Created by " + data["creator"].asString().unwrapOrDefault() +
            "\n"
            "\n" "No description provided..."
        );
    }

    Modpack(std::filesystem::path path = "") {
        data["name"] = GameManager::get()->m_playerName.c_str() + std::string("'s modpack");
        data["creator"] = GameManager::get()->m_playerName.c_str();
        logo = CCSprite::create();

        if (cocos::fileExistsInSearchPaths(path.string().c_str())) {
            loadFromFile(path);
        }
    }
};

inline static Ref<Modpack> loadit_pack = nullptr; //auto load one if exists

class ModsLayer : public CCLayer {
public:

    class ModsList : public CCLayer {
    public:
        CREATE_FUNC(ModsList);
        virtual bool init() override {
            if (!CCLayer::init()) return false;
            this->addChildAtPosition(SimpleTextArea::create(
                "here goes a redirect layer\n \n \n sorry but there will be touch bugs\nif i switch scene from ModsLayer to new ModsLayer..."
            ), Anchor::Center, {}, false);
            return true;
        }
        virtual void onEnterTransitionDidFinish() override {
            CCLayer::onEnterTransitionDidFinish();
            queueInMainThread([&] { openModsList(); });
        }
    };

    auto inline static NEXT_SETUP_TYPE = std::string("");
    auto inline static NEXT_CUSTOM_SETUP = std::function<void(CCLayer*)>();

    auto inline static STATUS_TITLE = std::string("");
    auto inline static STATUS_PERCENTAGE = std::string("");
    auto inline static STATUS_SHOW_LIST = { "status-label", "status-percentage-label", "loading-spinner" };
    auto inline static HIDE_STATUS = false;
    auto inline static SHOW_RESTART_BUTTON = false;

    struct ModpackCreator {
        inline static Modpack* MODPACK;
        inline static std::map<std::string, Mod*> MODS_SELECTED;
        inline static Ref<CCNode> CONTAINER;
        inline static Ref<CCMenu> MENU;

        inline static std::function<void(std::string)> nav_panel_set_title_func;
        static void setTitle(auto title = std::string("")) {
            if (nav_panel_set_title_func) nav_panel_set_title_func(title);
        }

        static void popupCustomSetup(Ref<FLAlertLayer> popup) {
            popup->m_noElasticity = true;
            if (auto a = CCScene::get()->querySelector("ModsLayer")) popup->m_scene = a;
            if (popup->m_mainLayer) {
                popup->m_mainLayer->setAnchorPoint(CCPointMake(0.5f, 0.f));
                popup->m_mainLayer->setPositionY(8.f);
                if (auto bg = popup->m_mainLayer->getChildByType<CCScale9Sprite>(0)) {
                    bg->setContentWidth(6443.f);
                    bg->setColor(ccc3(0, 0, 0));
                    bg->setOpacity(190);
                }
                if (auto title = popup->m_mainLayer->getChildByType<CCLabelBMFont>(0)) {
                    title->setFntFile("geode.loader/mdFontB.fnt");
                }
                if (auto mdArea = popup->m_mainLayer->getChildByType<MDTextArea>(0)) {
                    if (auto bg = mdArea->getChildByType<CCScale9Sprite>(0)) {
                        bg->setColor(ccc3(255, 255, 255));
                        bg->setOpacity(255);
                        auto size = bg->getContentSize();
                        bg->removeAllChildren();
                        bg->initWithFile("border_square.png"_spr);
                        bg->setContentSize(size + CCSizeMake(3, 3));
                    }
                }
                if (popup->m_buttonMenu) {
                    popup->m_buttonMenu->setAnchorPoint(CCPointMake(0.25f, 0.5f));
                    findFirstChildRecursive<ButtonSprite>(
                        popup->m_buttonMenu, [](ButtonSprite* node) {
                            if (auto a = node->m_BGSprite) a->setVisible(false);
                            if (auto a = node->m_label) a->setFntFile("chatFont.fnt");
                            return false;
                        }
                    );
                }
            }
        }

        static void start() {
            MODS_SELECTED.clear();
            if (MENU) MENU->removeFromParent();

            if (MODPACK) delete MODPACK;
            MODPACK = new Modpack();

            MENU = CCMenu::create();
            MENU->setZOrder(999);
            MENU->setID("MODPACK_CREATOR"_spr);
            MENU->setUserObject("MODPACK"_spr, MODPACK);
            MENU->setPositionY(MENU->getContentHeight() - (320.000 - 308.000));
            MENU->setAnchorPoint(CCPointZero);
            MENU->setContentWidth(486.000);
            limitNodeWidth(MENU, CCScene::get()->getContentWidth() * 0.8f, 1.f, 0.1f);

            CONTAINER = CCNode::create();
            CONTAINER->setPositionX(-9999.f);
            CONTAINER->setID("CONTAINER"_spr);
            MENU->addChild(CONTAINER);

            auto title = SimpleTextArea::create("huh");
            title->setAlignment(kCCTextAlignmentCenter);
            title->setID("title"_spr);
            MENU->addChild(title);
            nav_panel_set_title_func = [title = Ref(title)](std::string str) {
                if (title) title->setText(str);
                };

            auto bg = CCScale9Sprite::create("square02b_small.png");
            bg->setContentSize({ 486.000, 888.f});
            bg->setOpacity(90);
            bg->setColor(ccBLACK);
            bg->setPosition(CCPointMake(0.f, -10.f));
            bg->setAnchorPoint(CCPointMake(0.5f, 0.f));
            bg->setID("bg"_spr);
            MENU->addChild(bg, -1);

            step1();

            ModsLayer::NEXT_SETUP_TYPE = "setupForSelector";
            switchToScene(ModsList::create());
        }

        static void create() {

            static Ref<MDPopup> progress_popup;
            static Ref<MDTextArea> mdArea;
            static std::string mdAreaStr;
            if (!progress_popup or !progress_popup->isRunning()) {
                mdAreaStr = "```";
                progress_popup = MDPopup::create("creating modpack...", mdAreaStr, "close");
                popupCustomSetup(progress_popup.data());
                progress_popup->show();

                progress_popup->runAction(CCRepeatForever::create(CCSpawn::create(CallFuncExt::create(
                    [] {
                        auto scrollea = mdArea->getScrollLayer()->m_contentLayer->getPositionY();
                        if (mdAreaStr != mdArea->getString()) mdArea->setString(mdAreaStr.c_str());
                        mdArea->getScrollLayer()->m_contentLayer->setPositionY(scrollea);
                    }
                ), CCDelayTime::create(0.3f), nullptr)));

                mdArea = progress_popup->m_mainLayer->getChildByType<MDTextArea>(0);

                return std::thread(create).detach();
            }

#define logToMDPopup(str, ...) { log::info(str, __VA_ARGS__);\
                mdAreaStr = ((mdAreaStr + std::string("\n") + fmt::format(str, __VA_ARGS__)).c_str());\
            }

            auto filename = MODPACK->data["name"].asString().unwrapOrDefault();

            std::ranges::for_each(filename, [](char& c) {
                if (!std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '-')
                    c = '_';
                });

            logToMDPopup("creating \"{}\" pack", filename);

            auto list_path = getMod()->getConfigDir() / (filename + ".geode_modlist");
            auto pack_path = getMod()->getConfigDir() / (filename + ".geode_modpack");

            auto packit = false;
            auto zipper = file::CCMiniZFile::create(pack_path.string()).unwrapOrDefault();

            auto& list = MODPACK->data;

            //xd
            MODS_SELECTED.erase("geode.loader");
            MODS_SELECTED.erase(getMod()->getID());

            for (auto sel : MODS_SELECTED) {
                if (!mdArea or !mdArea->isRunning()) {
                    packit = 0;
                    continue;
                }
                if (sel.second) {
                    logToMDPopup("adding files of {} (ptr ok? - {})", sel.first, (bool)sel.second);
                    packit = true;
                    auto packagep = sel.second->getPackagePath();
                    zipper->write(
                        packagep.string(), 
                        file::readBinary(std::filesystem::path() / "mods" / packagep.filename()).unwrapOrDefault()
                    );
                    logToMDPopup("package added, {}", sel.second->getPackagePath());
                    if (MODPACK->include_config) {
                        auto dir = dirs::getModConfigDir() / sel.second->getID() / "";
                        auto atzip = std::filesystem::path() / "config";
                        logToMDPopup("adding files from {}", dir);
                        for (auto path : file::readDirectory(dir, true).unwrapOrDefault()) {
                            auto id = sel.second->getID();
                            auto str = path.parent_path().string();
                            //00000000 mod.id/ ...........++
                            auto name = std::filesystem::path(path).filename();
                            auto rel = std::string(str.begin() + str.rfind(id), str.end());
                            if (path.has_filename()) zipper->write(
                                (atzip / rel / name).string(),
                                file::readBinary(path).unwrapOrDefault()
                            );
                        }
                    }
                    if (MODPACK->include_saves) {
                        auto dir = dirs::getModsSaveDir() / sel.second->getID() / "";
                        auto atzip = std::filesystem::path() / "saves";
                        logToMDPopup("adding files from {}", dir);
                        for (auto path : file::readDirectory(dir, true).unwrapOrDefault()) {
                            auto id = sel.second->getID();
                            auto str = path.parent_path().string();
                            //00000000 mod.id/ ...........++
                            auto name = std::filesystem::path(path).filename();
                            auto rel = std::string(str.begin() + str.rfind(id), str.end());
                            if (path.has_filename()) zipper->write(
                                (atzip / rel / name).string(),
                                file::readBinary(path).unwrapOrDefault()
                            );
                        }
                        //todo ��� ������, ����?
                    }
                }
                logToMDPopup("adding {} entry", sel.first);
                auto entry = matjson::Value();
                if (Loader::get()->isModInstalled(sel.first)) {
                    auto mod = Loader::get()->getInstalledMod(sel.first);

                    if (MODPACK->include_settings_data and mod->hasSettings()) {
                        entry["settings"] = mod->getSavedSettingsData();
                        for (auto& a : entry["settings"]) if(string::containsAny(
                            string::toLower(a.getKey().value_or("").data()),
                            { "token" }
                        )) entry["settings"].erase(a.getKey().value_or(""));
                    };

                    if (MODPACK->include_saved_data and mod->getSaveContainer().size()) {
                        entry["saved"] = mod->getSaveContainer();
                        for (auto& a : entry["saved"]) if(string::containsAny(
                            string::toLower(a.getKey().value_or("").data()),
                            { "token" }
                        )) entry["saved"].erase(a.getKey().value_or(""));
                    };

                }
                logToMDPopup("{} entry: {}", sel.first, entry.dump());
                list["entries"][sel.first] = entry;

                logToMDPopup("{} entry added!\n", sel.first);
            }

            auto err = std::error_code();
            if (!packit) std::filesystem::remove(pack_path, err);
            if (err.message().size()) log::error("({}) {}", err.value(), err.message());

            if (!mdArea or !mdArea->isRunning()) return;

            logToMDPopup("{}", "creating list...");
            if (packit) zipper->write("this.geode_modlist", list.dump());
            else file::writeString(list_path, list.dump());

            if (packit) zipper->save();

            std::filesystem::path result_path = packit ? pack_path : list_path;
            auto result_name = std::filesystem::path(result_path).filename();
            logToMDPopup(
                "```\n- created \"[{}](file://{})\" pack!", 
                string::replace(result_name.string(), "\\", "/"), 
                string::replace(result_path.string(), "\\", "/")
            );

        }

        static void step3() {
            auto body_stream = std::stringstream();

            body_stream << std::string(
                "### Metadata:"
            ) << std::endl;
            body_stream << fmt::format(
                "- [name](http://e.ee): {}", MODPACK->data["name"].asString().unwrapOrDefault()
            ) << std::endl;
            body_stream << fmt::format(
                "- [creator](http://e.ee): {}", MODPACK->data["creator"].asString().unwrapOrDefault()
            ) << std::endl;

            body_stream << std::string(
                "### Options:"
            ) << std::endl;
            body_stream << fmt::format(
                "- [include settings data](http://e.ee): {} (supports modlist)", MODPACK->include_settings_data
            ) << std::endl;
            body_stream << fmt::format(
                "- [include saved data](http://e.ee): {} (supports modlist)", MODPACK->include_saved_data
            ) << std::endl;
            body_stream << fmt::format(
                "- [include config](http://e.ee): {} (only works for modpacks)", MODPACK->include_config
            ) << std::endl;
            body_stream << fmt::format(
                "- [include saves](http://e.ee): {} (only works for modpacks)", MODPACK->include_saves
            ) << std::endl;

            body_stream << std::string(
                "### Selected mods:"
            ) << std::endl;
            for (auto mod : MODS_SELECTED) body_stream << fmt::format(
                "- [{0}](mod:{0}) \n", mod.first
            );
            body_stream << "";

            static Ref<MDPopup> popup;
            popup = MDPopup::create("CREATING MODPACK:", body_stream.str(), " back", "create!",
                [](bool confirm) {
                    not confirm ? step2() : create();
                }
            );

            static CCPoint mdAreaPos;

            static Ref<MDTextArea> mdArea;
            if (popup->m_mainLayer) mdArea = popup->m_mainLayer->getChildByType<MDTextArea>(0);
            if (mdArea) mdArea->getScrollLayer()->m_contentLayer->setPosition(
                not mdAreaPos.isZero() ? mdAreaPos : mdArea->getScrollLayer()->m_contentLayer->getPosition()
            );

            static auto recreate_poup = []() {
                if (mdArea) mdAreaPos = mdArea->getScrollLayer()->m_contentLayer->getPosition();
                if (popup) popup->removeMeAndCleanup();
                step3();
                };

            typedef TextLinkedButtonWrapper LinkItem;
            #define assign_to_link(str, func, ...)                               \
            findFirstChildRecursive<LinkItem>(                                   \
                popup, [__VA_ARGS__](LinkItem* link) {                           \
                    if (link->getString() == std::string(str)) {                 \
                        CCMenuItemExt::assignCallback<LinkItem>(                 \
                            link, [__VA_ARGS__](LinkItem* link) {                \
                                func();                                          \
                            }                                                    \
                        );                                                       \
                    }                                                            \
                    return false;                                                \
                }                                                                \
            );                                                                   \

            class ValueSetPopup : public geode::Popup<std::string const&> {
            protected:
                bool setup(std::string const& key) override {

                    this->setTitle(fmt::format("editing value for {} key", key));

                    popupCustomSetup(this);

                    this->setCloseButtonSpr(
                        reinterpret_cast<CCSprite*>(SimpleTextArea::create("CLOSE")->getLines()[0])
                    );

                    m_buttonMenu->setAnchorPoint({ 0.500f, 0.500f });

                    auto warn_text = "warn: any changes being saved instainly!";
                    auto warn = SimpleTextArea::create(warn_text)->getLines()[0];
                    warn->setColor(ccc3(255, 127, 127));
                    warn->setOpacity(255);
                    warn->setScale(0.825f);
                    warn->setAnchorPoint(CCPointMake(0.5f, 0.2f));
                    m_buttonMenu->addChildAtPosition(warn, Anchor::Center, { 0, 6.f });

                    auto input = TextInput::create(
                        m_mainLayer->getContentWidth() - 22.f, 
                        key, "geode.loader/mdFont.fnt"
                    );
                    input->setCommonFilter(CommonFilter::Any);
                    m_buttonMenu->addChildAtPosition(input, Anchor::Bottom, {0, 30.f});

                    input->setString(MODPACK->data[key].dump());
                    input->setCallback([key, warn, warn_text](std::string const& str) {

                        warn->setString(warn_text);

                        auto parse = matjson::parse(str);
                        if (auto err = parse.err()) warn->setString(fmt::format(
                            "parse err: {}", err.value().message
                        ).c_str());
                        else MODPACK->data[key] = parse.unwrapOrDefault();

                        });

                    input->focus();

                    handleTouchPriority(this);

                    return true;
                }

            public:
                static ValueSetPopup* create(std::string const& key) {
                    auto ret = new ValueSetPopup();
                    recreate_poup();
                    if (popup) ret->m_scene = popup->m_scene;
                    if (popup) popup->removeMeAndCleanup();
                    if (MODPACK and ret->initAnchored(310.f, 90.f, key)) {
                        ret->autorelease();
                        return ret;
                    }

                    delete ret;
                    return nullptr;
                }
                void keyDown(cocos2d::enumKeyCodes key) override {
                    if (key == cocos2d::enumKeyCodes::KEY_Escape) return this->onClose(nullptr);
                    if (key == cocos2d::enumKeyCodes::KEY_Enter) return this->onClose(nullptr);
                    if (key == cocos2d::enumKeyCodes::KEY_NumEnter) return this->onClose(nullptr);
                    return Popup::keyDown(key);
                }
                void onClose(cocos2d::CCObject* asd) override {
                    this->Popup::onClose(asd);
                    recreate_poup();
                }
            };

            LinkItem* link; //IntelliSence...
            assign_to_link("name",[link] {
                ValueSetPopup::create(link->getString())->show();
                });
            assign_to_link("creator", [link] {
                ValueSetPopup::create(link->getString())->show();
                });
            assign_to_link("include settings data", [link] {
                MODPACK->include_settings_data = !MODPACK->include_settings_data;
                recreate_poup();
                });
            assign_to_link("include saved data", [link] {
                MODPACK->include_saved_data = !MODPACK->include_saved_data;
                recreate_poup();
                });
            assign_to_link("include config", [link] {
                MODPACK->include_config = !MODPACK->include_config;
                recreate_poup();
                });
            assign_to_link("include saves", [link] {
                MODPACK->include_saves = !MODPACK->include_saves;
                recreate_poup();
                });

            popupCustomSetup(popup.data());
            popup->show();

            while (MENU->getChildByType<CCMenuItem>(-1))
                MENU->getChildByType<CCMenuItem>(-1)->removeFromParent();

            auto back = CCMenuItemExt::createSpriteExtra(
                SimpleTextArea::create("BACK")->getLines()[0], [](CCMenuItem* item) {
                    step2();
                }
            );
            back->setPosition(CCPointMake(-218.f, 0.f));
            back->setID("back"_spr);
            MENU->addChild(back);

            setTitle("Step 3: final setup!");
        }

        static void step2() {
            //class MyPopup : public geode::Popup<std::string const&> {};
            auto body_stream = std::string("Your modpack will be created as **simple list file if** all entries will be **without files**, **or** will be created **zip archive** with .geode files and list file.\n");
            body_stream += "### [TOGGLE ALL](https://e.ee)\n";
            for (auto mod : MODS_SELECTED) body_stream += mod.second ?
                fmt::format(
                    "- [{0}](mod:{0}) \n", 
                    mod.first.size() > 64 ? std::string(mod.first.begin(), mod.first.begin() + 61) + "..." : mod.first,
                    mod.first
                )
                : //mod ptr?
                fmt::format(
                    "- [{0}](mod:{0}) <c-999>only as id without files</c>\n", mod.first
                );
            body_stream += "";

            auto popup = MDPopup::create("SELECTED MODS:", body_stream, " back", "confirm",
                [](bool confirm) {
                    not confirm ? step1(true) : step3();
                }
            );
            typedef TextLinkedButtonWrapper LinkItem;
            findFirstChildRecursive<LinkItem>(
                popup, [](LinkItem* link) {
                    if (link->getString() == std::string("TOGGLE ALL")) {
                        CCMenuItemExt::assignCallback<LinkItem>(
                            link, [](LinkItem* link) {
                                if (link->getParent()) findFirstChildRecursive<LinkItem>(
                                        link->getParent(), [](LinkItem* link) {
                                            if (MODS_SELECTED.contains(link->getString())) 
                                                link->activate();
                                            return false;
                                        }
                                    );
                            }
                        );
                    }
                    if (MODS_SELECTED.contains(link->getString())) {
                        auto id = link->getString();
                        auto saved_modptr = MODS_SELECTED.at(id);
                        if (not saved_modptr) {
                            link->selected();
                            link->setEnabled(false);
                            return false;
                        }
                        auto mark = SimpleTextArea::create("as id including files");
                        mark->setScale(1.350);
                        mark->setAnchorPoint(CCPointMake(0.f, 0.05f));
                        link->addChildAtPosition(mark, Anchor::BottomRight, {7.f, 0.f}, false);
                        CCMenuItemExt::assignCallback<LinkItem>(
                            link, [mark, id, saved_modptr](LinkItem* link) {
                                if (mark->getText() == "as id including files") {
                                    MODS_SELECTED[id] = nullptr;
                                    mark->setText("as id without files");
                                }
                                else {
                                    MODS_SELECTED[id] = saved_modptr;
                                    mark->setText("as id including files");
                                }
                            }
                        );
                    }
                    return false;
                }
            );
            popupCustomSetup(popup);
            popup->show();

            while (MENU->getChildByType<CCMenuItem>(-1))
                MENU->getChildByType<CCMenuItem>(-1)->removeFromParent();

            auto back = CCMenuItemExt::createSpriteExtra(
                SimpleTextArea::create("BACK")->getLines()[0], [](CCMenuItem* item) {
                    step1(true);
                }
            );
            back->setPosition(CCPointMake(-218.f, 0.f));
            back->setID("back"_spr);
            MENU->addChild(back);

            setTitle("Step 2: toggle entries type. (file/id)");
        }

        static void step1(bool activate_next = false) {

            while (MENU->getChildByType<CCMenuItem>(-1))
                MENU->getChildByType<CCMenuItem>(-1)->removeFromParent();
            
            auto next = CCMenuItemExt::createSpriteExtra(
                SimpleTextArea::create("NEXT")->getLines()[0], [](CCMenuItem* item) {
                    auto body_stream = std::string("### [ADD LOADED MODS](http://e.ee) [REMOVE ALL](http://e.ee)\n");
                    for (auto mod : MODS_SELECTED) body_stream += fmt::format(
                        "- [{}](mod:{}){}\n",
                        mod.first.size() > 38 ? std::string(mod.first.begin(), mod.first.begin() + 34) + "..." : mod.first,
                        mod.first,
                        mod.second ? 
                        ", able to save as .geode file</c>" : "<c-f99> that not installed (or undefined)</c>"
                    );
                    auto popup = MDPopup::create("SELECTED MODS:", body_stream, "close", "confirm",
                        [](bool confirm) {
                            not confirm ? void() : step2();
                        }
                    );
                    typedef TextLinkedButtonWrapper LinkItem;
                    findFirstChildRecursive<LinkItem>(
                        popup, [item, popup](LinkItem* link) {
                            if (link->getString() == std::string("ADD LOADED MODS")) {
                                CCMenuItemExt::assignCallback<LinkItem>(
                                    link, [item, popup](LinkItem* link) {
                                        MODS_SELECTED.clear();
                                        for (auto mod : Loader::get()->getAllMods()) {
                                            if (mod->isOrWillBeEnabled()) 
                                                MODS_SELECTED[mod->getID()] = mod;
                                        }
                                        popup->onBtn1(item);
                                        item->activate();
                                    }
                                );
                            }
                            if (link->getString() == std::string("REMOVE ALL")) {
                                CCMenuItemExt::assignCallback<LinkItem>(
                                    link, [item, popup](LinkItem* link) {
                                        MODS_SELECTED.clear();
                                        popup->onBtn1(item);
                                        item->activate();
                                    }
                                );
                            }
                            return false;
                        }
                    );
                    popupCustomSetup(popup);
                    popup->show();
                }
            );
            activate_next ? next->activate() : void();
            next->setPosition(CCPointMake(218.f, 0.f));
            next->setID("next"_spr);
            MENU->addChild(next);

            setTitle("Step 1: select mods for pack.");
        }

    };

    inline static void installPack(Modpack* pack, bool restart = false) {

        if (pack->data.contains("files_installed")) void();
        else {
            pack->data["files_installed"] = true;

            auto unzip_path = dirs::getTempDir() / ZipUtils::base64URLEncode(pack->data["name"].dump()).c_str();
            if (auto unzip = file::CCMiniZFile::create(pack->path.string())) {
                unzip.unwrapOrDefault()->extractAll(unzip_path.string());

                auto options =
                    std::filesystem::copy_options::recursive |
                    std::filesystem::copy_options::overwrite_existing;
                std::error_code err;

                std::filesystem::copy(unzip_path / "mods", dirs::getModsDir(), options, err);
                std::filesystem::copy(unzip_path / "config", dirs::getModConfigDir(), options, err);
                std::filesystem::copy(unzip_path / "saves", dirs::getModsSaveDir(), options, err);
            };
        }

        if (pack->data.contains("install_progress")) void();
        else {
            pack->data["install_progress"] = pack->data["entries"];
        }

        auto id = std::string();
        for (auto val : pack->data["install_progress"]) {
            id = val.getKey().value_or("");
            if (val.contains("settings")) {
                file::writeString(dirs::getModsSaveDir() / id / "settings.json", val["settings"].dump());
            }
            if (val.contains("saved")) {
                file::writeString(dirs::getModsSaveDir() / id / "saved.json", val["saved"].dump());
            }
            break;
        }
        pack->data["install_progress"].erase(id);

        auto mod_package = (dirs::getModsDir() / (id + ".geode"));
        if (fileExistsInSearchPaths(mod_package.string().c_str())) return installPack(pack, restart);

        STATUS_PERCENTAGE = "0%  ";

        STATUS_TITLE = id;
        HIDE_STATUS = STATUS_TITLE.empty();

        if (HIDE_STATUS) {
            SHOW_RESTART_BUTTON = true;
            if (restart) game::restart();
            return;
        }

        std::string ver = "latest";
        auto url = "https://api.geode-sdk.org/v1/mods/" +id+ "/versions/"+ ver + "/download";

        auto req = web::WebRequest();
        auto listener = new EventListener<web::WebTask>;
        listener->bind(
            [id, pack, restart](web::WebTask::Event* e) {
                if (web::WebProgress* prog = e->getProgress()) {
                    STATUS_PERCENTAGE = fmt::format("{}%  ", (int)prog->downloadProgress().value_or(0.f));
                }
                if (web::WebResponse* res = e->getValue()) {
                    std::string data = res->string().unwrapOr("no res");
                    if (res->code() < 399) {
                        res->into(dirs::getModsDir() / (id + ".geode"));
                    }
                    installPack(pack, restart);
                }
            }
        );
        listener->setFilter(req.send("GET", url));
    };

    void setupForSelector() {
        if (auto wiwi = typeinfo_cast<CCMenuItem*>(this->querySelector("back-button"))) {
            //go back to PacksList.
            //and delete ModpackCreator
            CCMenuItemExt::assignCallback<CCNode>(wiwi, [](CCNode*) {
                ModpackCreator::MENU = nullptr;
                NEXT_SETUP_TYPE = "setupForPacksList";
                switchToScene(ModsList::create());
                });
        }
    }

    void setupForPacksList() {

        if (auto bg = typeinfo_cast<CCLayerColor*>(this->querySelector("frame-bg"))) {
            auto color = bg->getColor();
            dark_themed = !((color.r + color.g + color.b) / 255);
        }

        if (auto wiwi = this->querySelector("ModList")) {
            wiwi->setVisible(!wiwi->m_bVisible);

            auto scroll = ScrollLayer::create(wiwi->getContentSize());
            scroll->setID("modpacks_list"_spr);
            scroll->setPosition(CCPointMake(15.f, 0.5f));//fffffffuuuUUUUuck
            if (auto parent = wiwi->getParent()) parent->addChild(scroll);

            auto files = file::readDirectory(getMod()->getConfigDir(), true).unwrapOrDefault();
            if (loadit_pack) files.push_back(loadit_pack->path);
            for (auto file : files) {
                auto menu = CCMenu::create();
                menu->setContentHeight(46.000f);
                menu->setContentWidth(scroll->getContentWidth());

                scroll->m_contentLayer->addChild(menu);

                auto modpack = new Modpack(file);
                menu->setUserObject("modpack", modpack);

                auto container = CCNode::create();
                container->setContentSize(menu->getContentSize());
                container->setAnchorPoint(CCPointZero);

                auto bg = CCScale9Sprite::create("square02b_small.png");
                bg->setContentSize(menu->getContentSize() - CCSizeMake(6, 6));
                bg->setOpacity(dark_themed ? 25 : 90);
                bg->setColor(dark_themed ? ccWHITE : ccBLACK);
                container->addChildAtPosition(bg, Anchor::Center, {}, false);

                auto logo = modpack->logo;
                logo->runAction(CCRepeatForever::create(CCSpawn::create(CallFuncExt::create(
                    [logo] {
                        if (logo) logo->setAnchorPoint(CCPointMake(0.f, 0.5f));
                        if (logo) limitNodeSize(logo, CCSizeMake(1, 1) * 32.f, 1337.f, 0.1f);
                    }
                ), nullptr)));
                limitNodeSize(logo, CCSizeMake(1, 1) * 32.f, 1337.f, 0.1f); ///fffffuck *offset
                container->addChildAtPosition(logo, Anchor::Left, { 8.000f, 0 }, false);

                auto offset = logo->boundingBox().size.width + 16.f;

                auto name = SimpleTextArea::create(
                    modpack->data["name"].asString().unwrapOrDefault(), "bigFont.fnt", 0.500f
                )->getLines()[0];
                limitNodeWidth(name, 300.000f, name->getScale(), 0.1f);
                name->setAnchorPoint(CCPointMake(0.f, 0.85f));
                container->addChildAtPosition(name, Anchor::Left, { offset, 14.f}, false);

                auto creator = SimpleTextArea::create(
                    "By: " + modpack->data["creator"].asString().unwrapOrDefault(), "goldFont.fnt", 0.42f
                )->getLines()[0];
                limitNodeWidth(creator, 296.000f, creator->getScale(), 0.1f);
                creator->setAnchorPoint(CCPointMake(0.f, -0.15f));
                container->addChildAtPosition(creator, Anchor::Left, { offset, -12.f }, false);

                auto item = CCMenuItemExt::createSpriteExtra(container,
                    [file](CCNode*) {
                        auto popup = openSettingsPopup(
                            Loader::get()->getInstalledMod("geode.loader"), false
                        );
                        findFirstChildRecursive<CCNode>(
                            popup, [&](CCNode* node){
                                if (typeinfo_cast<CCMenuItem*>(node)) node->setVisible(false);
                                if (node == popup) return false;
                                if (node->getParent() != popup->m_mainLayer) return false;
                                node->setVisible(false);
                                return false;
                            }
                        );
                        auto menu = popup->m_buttonMenu;
                        auto layer = popup->m_mainLayer;

                        menu->setVisible(true);
                        if (auto close = menu->getChildByType<CCMenuItem>(0)) {
                            close->setVisible(true);
                        }

                        layer->setVisible(true);
                        if (auto bg = layer->getChildByType<CCScale9Sprite>(0)) {
                            bg->setVisible(true);
                        }

                        auto modpack = new Modpack(file);
                        popup->setUserObject("modpack"_spr, modpack);

                        auto topBG = CCLayerColor::create({ 0,0,0,90 });
                        topBG->setID("topBG"_spr);
                        topBG->setContentWidth(menu->getContentWidth() - (440.000 - 435.000));
                        topBG->setContentHeight(57.000f);
                        topBG->setZOrder(-1);
                        menu->addChildAtPosition(topBG, Anchor::TopLeft, { 3.f, -68.000f }, false);

                        auto logo = modpack->logo;
                        logo->setID("logo"_spr);
                        logo->runAction(CCRepeatForever::create(CCSpawn::create(CallFuncExt::create(
                            [logo] {
                                if (logo) logo->setAnchorPoint(CCPointMake(0.f, 0.5f));
                                if (logo) limitNodeSize(logo, CCSizeMake(1, 1) * 48.000f, 1337.f, 0.1f);
                            }
                        ), nullptr)));

                        menu->addChildAtPosition(logo, Anchor::TopLeft, { 30.f, -40.f }, false);

                        auto name = SimpleTextArea::create(
                            modpack->data["name"].asString().unwrapOrDefault(), "bigFont.fnt", 0.600f
                        )->getLines()[0];
                        name->setID("name"_spr);
                        limitNodeWidth(name, 226.000f, name->getScale(), 0.1f);
                        name->setAnchorPoint(CCPointMake(0.f, 0.85f));
                        menu->addChildAtPosition(name, Anchor::TopLeft, { 88.f, -19.f }, false);

                        auto creator = SimpleTextArea::create(
                            "By: " + modpack->data["creator"].asString().unwrapOrDefault(), "goldFont.fnt", 0.52f
                        )->getLines()[0];
                        creator->setID("creator"_spr);
                        limitNodeWidth(creator, 226.000f, creator->getScale(), 0.1f);
                        creator->setAnchorPoint(CCPointMake(0.f, -0.15f));
                        menu->addChildAtPosition(creator, Anchor::TopLeft, { 88.000f, -49.000f }, false);

                        auto file = SimpleTextArea::create(
                            std::filesystem::path(modpack->path).filename().string(), "chatFont.fnt", 0.52f
                        )->getLines()[0];
                        file->setID("file"_spr);
                        limitNodeWidth(file, 226.000f, file->getScale(), 0.1f);
                        file->setAnchorPoint(CCPointMake(0.f, -0.15f));
                        menu->addChildAtPosition(file, Anchor::TopLeft, { 88.000f, -62.000f }, false);

                        auto about = MDTextArea::create(modpack->about, { 280.f, 198.f});
                        about->setID("about"_spr);
                        about->ignoreAnchorPointForPosition(1);
                        menu->addChildAtPosition(about, Anchor::BottomLeft, { 16.000f, 10.000f }, false);

                        auto is_installed = CCBool::create(true);
                        popup->setUserObject("is_installed"_spr, is_installed);

                        auto infstream = std::stringstream();
                        infstream << "##### [EDIT PACK](http://e.ee) [DELETE](http://e.ee)" << std::endl;
                        if (modpack->include_settings_data) infstream << "### Includes settings data" << std::endl;
                        if (modpack->include_saved_data) infstream << "### Includes saved data" << std::endl;
                        infstream << "## Mods list:" << std::endl;
                        for (auto val : modpack->data["entries"]) {
                            auto id = val.getKey().value_or("");

                            infstream << fmt::format("\n\n [{0}](mod:{0})", id);
                            if (val.contains("settings")) infstream << " `[settings]`";
                            if (val.contains("saved")) infstream << " `[saved_data]`";
                            infstream << std::endl;

                            if (not Loader::get()->getInstalledMod(id)) is_installed->setValue(false);
                        }

                        auto inf = MDTextArea::create(infstream.str(), {139.000f, 198.f});
                        inf->setID("inf"_spr);
                        inf->ignoreAnchorPointForPosition(1);
                        menu->addChildAtPosition(inf, Anchor::BottomRight, { -139.000f -1, 10.000f }, false);

                        auto popup_really = popup;
                        {
                            typedef TextLinkedButtonWrapper LinkItem;
                            LinkItem* link; //IntelliSence...
                            auto popup = inf;
                            assign_to_link(
                                "EDIT PACK", [&] {
                                    MDPopup::create("Pack editing...",
                                        """" "Pack edit UI is planned, but for now its goes manually. "
                                        """" "Packs takes their places at mod config folder. "
                                        "\n" "- \".geode_modlist\" ones is .json text files"
                                        "\n" "- \".geode_modpack\" ones is .zip archive files"
                                        "\n"
                                        "\n" "You can open them using \"Open As\" function in your file manager."
                                        "\n"
                                        "\n" "### .geode_modpack tips"
                                        "\n" "- You can add logo.png or pack.png"
                                        "\n" "- You can add about.md or README.md"
                                        "\n"
                                        "\n" "### .geode_modlist tips"
                                        "\n" "- You can add logo json key with texture/frame name or.. LINK!)"
                                        "\n" "```"
                                        "\n"
                                        R"({
    "name": "awful mods",
    "creator": "me",
    "logo": "https://images2.imgbox.com/66/b5/erYMNC8O_o.png",
    "entries": ...
                                        )"
                                        "\n" "```"
                                        , "OK")->show();
                                }
                            );
                            assign_to_link(
                                "DELETE", [modpack] {
                                    auto path = (const char*)modpack->path.u8string().c_str();
                                    auto err = std::remove(path);
                                    if (err) log::error("remove err{} for {}", err, path);
                                    NEXT_SETUP_TYPE = "setupForPacksList";
                                    switchToScene(ModsList::create());
                                }, modpack
                            );
                        };
                        
                        auto btn_ref = findFirstChildRecursive<ButtonSprite>(popup, [](CCNode*) { return true; });
                        btn_ref->setString(is_installed->getValue() ? "Uninstall" : "Install");
                        btn_ref->setScale(0.825f);
                        btn_ref->setID("setup_btn_ref"_spr);
                        auto setup = CCMenuItemExt::createSpriteExtra(
                            btn_ref, [popup, btn_ref, modpack, is_installed](CCNode*) {
                                if (is_installed->getValue()) {
                                    for (auto val : modpack->data["entries"]) {
                                        auto id = val.getKey().value_or("");
                                        auto mod = Loader::get()->getInstalledMod(id);
                                        if (!mod) continue;
                                        mod->uninstall(val.contains("settings") or val.contains("saved"));
                                    }
                                    SHOW_RESTART_BUTTON = true;
                                }
                                else {
                                    popup->removeFromParent();
                                    installPack(modpack);
                                };
                                is_installed->setValue(!is_installed->getValue());
                                btn_ref->setString(is_installed->getValue() ? "Uninstall" : "Install");
                            }
                        );
                        setup->setID("setup_btn"_spr);
                        menu->addChildAtPosition(setup, Anchor::TopRight, { -70.000f, -38.000f }, false);

                        handleTouchPriority(popup);

                    }
                );
                item->setContentWidth(item->getContentWidth() / 2);
                item->setAnchorPoint({ 1.f, 0.5f });
                item->m_scaleMultiplier = 0.95f;
                menu->addChildAtPosition(item, Anchor::Center, {}, false);
            }
            scroll->m_contentLayer->setLayout(RowLayout::create()
                ->setCrossAxisAlignment(AxisAlignment::End)
                ->setCrossAxisOverflow(true)
                ->setGrowCrossAxis(true)
                ->setAxisReverse(true)
                ->setGap(-3.f)
            );
            scroll->moveToTop();

            static auto last_pos = CCPointMake(0, 0);
            static auto last_size = CCSizeMake(0, 0);
            scroll->runAction(CCRepeatForever::create(CCSpawn::create(CallFuncExt::create([scroll] {
                last_pos = scroll->m_contentLayer->getPosition();
                last_size = scroll->m_contentLayer->getContentSize();
                }), CCDelayTime::create(0.1f), nullptr)));
            if (not last_pos.isZero() and last_size.equals(scroll->m_contentLayer->getContentSize()))
                scroll->m_contentLayer->setPosition(last_pos);
        }

        if (auto wiwi = this->querySelector("list-actions-menu")) wiwi->setVisible(!wiwi->m_bVisible);
        if (auto wiwi = this->querySelector("right-actions-menu")) wiwi->setVisible(!wiwi->m_bVisible);
        if (auto wiwi = this->querySelector("page-menu")) wiwi->setScale(0.f);//being updated a lot.

        if (auto wiwi = this->querySelector("actions-menu")) {
            wiwi->setVisible(!wiwi->m_bVisible);

            auto menu = CCMenu::create();
            menu->setID("actions-menu"_spr);
            menu->setPosition(wiwi->getPosition());
            menu->setAnchorPoint(wiwi->getAnchorPoint());
            menu->setContentSize(wiwi->getContentSize());
            if (wiwi->getParent()) wiwi->getParent()->addChild(menu);


            auto create_modpack_button = CCMenuItemExt::createSpriteExtraWithFrameName(
                "GJ_newBtn_001.png", 0.8f, [&](CCNode*) { ModpackCreator::start(); }
            );
            create_modpack_button->setID("create_modpack_button"_spr);
            menu->addChild(create_modpack_button);

            auto image51 = CircleButtonSprite::createWithSpriteFrameName(
                "gj_folderBtn_001.png", 1.f,
                dark_themed ? CircleBaseColor::DarkPurple : CircleBaseColor::Green
            );
            image51->setScale(0.8);
            auto modpacks_folder = CCMenuItemExt::createSpriteExtra(
                image51, [&](CCNode*) {
                    file::openFolder(getMod()->getConfigDir());
                }
            );
            modpacks_folder->setSprite((CCSprite*)modpacks_folder->getNormalImage());
            modpacks_folder->setID("modpacks_folder_button"_spr);
            menu->addChild(modpacks_folder);

            menu->setLayout(wiwi->getLayout());

        }

        if (auto wiwi = this->querySelector("tabs-menu")) { //add title

            wiwi->setVisible(!wiwi->m_bVisible);

            auto title = SimpleTextArea::create("Your Modpacks", "bigFont.fnt", 0.60f, 290.f);
            title->setID("title"_spr);
            title->setAlignment(CCTextAlignment::kCCTextAlignmentCenter);
            title->setPosition(wiwi->getPosition() + CCSizeMake(0, 5));
            title->setAnchorPoint(wiwi->getAnchorPoint());
            if (auto parent = wiwi->getParent()) parent->insertAfter(title, wiwi);

            //steal bg from installed button
            if (auto button = this->querySelector("installed-button"))
                if (auto btn_spr = button->getChildByType<CCNode>(0))
                    if (auto bg = btn_spr->getChildByType<CCScale9Sprite>(-1)) {
                        bg->setContentSize(title->getContentSize() + CCSizeMake(69, 16));
                        bg->setZOrder(-1);
                        bg->setVisible(true);
                        title->addChildAtPosition(bg, Anchor::Center, {}, false);
                    };
            if (auto menu = title->getChildByType<CCMenu>(0)) {
                //mod info btn
                auto info = CCMenuItemExt::createSpriteExtraWithFrameName("GJ_infoIcon_001.png", 0.7f,
                    [](CCNode*) {
                        openInfoPopup(getMod());
                    }
                );
                menu->addChildAtPosition(info, Anchor::BottomLeft, { 323.f, 16.f }, false);
            };
        }

        if (auto wiwi = typeinfo_cast<CCMenuItem*>(this->querySelector("back-button"))) {
            //go back to normal list first, not MenuLayer
            CCMenuItemExt::assignCallback<CCNode>(wiwi, [](CCNode*) { switchToScene(ModsList::create()); });
        }

    }

    void customSetup() {
        if (auto aw = typeinfo_cast<CCLayer*>(this)) {
            aw->setKeypadEnabled(0);
            aw->addChild(CustomKeypadListener::create(), -111);
            if (auto wiwi = typeinfo_cast<CCMenuItem*>(aw->querySelector("back-button"))) {
                aw->getChildByType<CustomKeypadListener>(0)->m_keyBackClicked = [wiwi]
                    {
                        if (wiwi) wiwi->activate();
                    };
            }
        }

        /*this->runAction(CCSequence::create(
            CCDelayTime::create(0.98),
            CallFuncExt::create(
                [this] {
                    if (auto btn = typeinfo_cast<CCMenuItem*>(this->querySelector("page-next-button")))
                        btn->activate();
                    if (auto btn = typeinfo_cast<CCMenuItem*>(this->querySelector("page-previous-button")))
                        btn->activate();
                }
            ),
            CallFuncExt::create(
                [this] {
                }
            ),
            nullptr
        ));*/

        if (ModpackCreator::MENU) this->addChild(ModpackCreator::MENU);

        if (auto status_bg = this->querySelector("mod-list-frame > ModsStatusNode > status-bg")) {
            status_bg->runAction(CCRepeatForever::create(CCSpawn::create(CallFuncExt::create([status_bg]
                {
                    if (!status_bg) return;
                    if (auto pModsStatusNode = status_bg->getParent()) {
                        auto restart_button = pModsStatusNode->querySelector("restart-button");
                        if (SHOW_RESTART_BUTTON) {
                            restart_button->setVisible(SHOW_RESTART_BUTTON);
                            restart_button->getParent()->updateLayout();
                        }
                    }
                    if (HIDE_STATUS) {
                        HIDE_STATUS = !HIDE_STATUS;
                        status_bg->setVisible(HIDE_STATUS);
                    }
                    if (not STATUS_TITLE.empty()) {
                        status_bg->setVisible(true);
                        for (auto id : STATUS_SHOW_LIST) if (auto a = status_bg->querySelector(id)) {
                            a->setVisible(true);
                        }
                        if (auto aw = typeinfo_cast<CCLabelBMFont*>(status_bg->querySelector("status-label"))) {
                            if (aw->getString() != STATUS_TITLE) aw->setString(STATUS_TITLE.c_str());
                            limitNodeSize(aw, CCSizeMake(416, 32.5), 1.f, 0.1f);
                        }
                        if (auto aw = typeinfo_cast<CCLabelBMFont*>(status_bg->querySelector("status-percentage-label"))) {
                            if (aw->getString() != STATUS_PERCENTAGE) aw->setString(STATUS_PERCENTAGE.c_str());
                        }
                    }
                }
            ), CCDelayTime::create(0.1f), nullptr)));
        }

        if (NEXT_SETUP_TYPE.size()) {
            if (NEXT_SETUP_TYPE == "setupForPacksList") setupForPacksList();
            if (NEXT_SETUP_TYPE == "setupForSelector") setupForSelector();
            NEXT_SETUP_TYPE = "";
        }
        else if (auto button_ref = this->querySelector("mods-folder-button")) {
            CircleButtonSprite* image = CircleButtonSprite::createWithSprite("btn_icon.png"_spr, 1.f + (0.445 - 0.362));
            if (auto a = button_ref->getChildByType<CircleButtonSprite>(0)) {
                image->setDisplayFrame(a->displayFrame());
                image->setScale(a->getScale());
            }
            if (auto parent = button_ref->getParent()) {
                auto modpacks_button = CCMenuItemExt::createSpriteExtra(
                    image, [&](CCNode*) {
                        NEXT_SETUP_TYPE = "setupForPacksList";
                        switchToScene(ModsList::create());
                    }
                );
                modpacks_button->setID("modpacks_button"_spr);
                parent->insertAfter(modpacks_button, button_ref);
                parent->updateLayout();
            }
        }

        if (NEXT_CUSTOM_SETUP) { NEXT_CUSTOM_SETUP(this); NEXT_CUSTOM_SETUP = nullptr; }
    }
};

#include <Geode/modify/CCLayer.hpp>
class $modify(ModsLayerExt, CCLayer) {
    bool init() {
        if (!CCLayer::init()) return false;
        if (typeinfo_cast<ModsLayer*>(this)) {
            queueInMainThread(
                [__this = Ref(this)] { reinterpret_cast<ModsLayer*>(__this.data())->customSetup(); }
            );
        }
        return true;
    }
};

#include <Geode/modify/MenuLayer.hpp>
class $modify(ModpackAutoInstall, MenuLayer) {
    static auto loadit(std::string file, CCScene * scene) {
        file = CCFileUtils::get()->fullPathForFilename(file.c_str(), 0).c_str();
        loadit_pack = new Modpack(file);
        if (getMod()->getSavedValue<uint32_t>("loadit_hash") == fnv1a_hash(file)) return scene;
        getMod()->setSavedValue<uint32_t>("loadit_hash", fnv1a_hash(file));
        ModsLayer::installPack(loadit_pack, true);
        scene = CCScene::create();

        scene->addChild(geode::createLayerBG(), -10);
        geode::addSideArt(scene);

        auto msg = SimpleTextArea::create(
            string::replace(
                "Intalling mod pack...\n(" + file + ")\n\nThe game will be restarted on finish."
                , "\\", "/" //windows
            ), "goldFont.fnt"
        );
        msg->setAlignment(kCCTextAlignmentCenter);
        scene->addChildAtPosition(msg, Anchor::Center, {}, false);

        auto progress_text = SimpleTextArea::create("");
        scene->runAction(CCRepeatForever::create(CCSpawn::create(CallFuncExt::create(
            [progress_text = Ref(progress_text)] {
                if (!progress_text) return;
                progress_text->setText(ModsLayer::STATUS_TITLE + " " + ModsLayer::STATUS_PERCENTAGE);
            }
        ), nullptr)));
        progress_text->setAnchorPoint({ 0.5f, -0.2f });
        progress_text->setAlignment(kCCTextAlignmentCenter);
        scene->addChildAtPosition(progress_text, Anchor::Bottom, {}, false);

        return scene;
    }
    static CCScene* scene(bool isVideoOptionsOpen) {
        auto scene = MenuLayer::scene(isVideoOptionsOpen);
        if (fileExistsInSearchPaths("loadit.geode_modpack")) return loadit("loadit.geode_modpack", scene);
        if (fileExistsInSearchPaths("loadit.geode_modlist")) return loadit("loadit.geode_modlist", scene);
        return scene;
    }
};

void modLoaded() {

    new EventListener<EventFilter<ModLogoUIEvent>>(+[](ModLogoUIEvent* event) {
        if (!event) return ListenerResult::Propagate;
        typedef ModsLayer::ModpackCreator MPC;
        auto id = event->getModID();
        auto mod = event->getMod().value_or(nullptr);
        if (MPC::MENU) {
            auto menu = CCMenu::create();
            menu->setPosition(CCPointMake(40.000, 10.000));
            event->getSprite()->addChild(menu);

            auto toggler = CCMenuItemExt::createTogglerWithStandardSprites(0.6f,
                [mod, id](CCMenuItemToggler* item) {
                    if (item and item->isToggled()) MPC::MODS_SELECTED.erase(id);
                    else MPC::MODS_SELECTED[id] = mod;
                }
            );
            toggler->setID("toggler"_spr);
            MPC::CONTAINER->addChild(toggler);//aaaa cleanups!
            MPC::CONTAINER->runAction(CCRepeatForever::create(CCSpawn::create(
                CCDelayTime::create(0.1f),//atleast...
                CallFuncExt::create(
                    [toggler, id] {
                        //log::debug("{} {}", id, toggler);
                        if (typeinfo_cast<CCNode*>(toggler)) {
                            toggler->toggle(MPC::MODS_SELECTED.contains(id));
                        }
                    }
                ),
                nullptr
            )));
            toggler->m_offButton->setOpacity(173);
            menu->addChild(toggler);
        };
        return ListenerResult::Propagate;
        });

    new EventListener<EventFilter<ModItemUIEvent>>(+[](ModItemUIEvent* event) {

        return ListenerResult::Propagate;
        });

}

$on_mod(Loaded) { modLoaded(); }