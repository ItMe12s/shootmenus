#include "World.hpp"

#include <Geode/ui/OverlayManager.hpp>

using namespace geode::prelude;

namespace shootmenus {
    constexpr float kHalf = 12.f;
    constexpr std::array kKeys{KEY_A, KEY_D, KEY_W, KEY_S};
    constexpr size_t kLeft = 0;
    constexpr size_t kRight = 1;
    constexpr size_t kUp = 2;
    constexpr size_t kDown = 3;

    World* World::create() {
        auto ret = new World();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    void World::toggle() {
        if (PlayLayer::get() || LevelEditorLayer::get()) return;
        auto overlay = OverlayManager::get();
        if (auto world = overlay->getChildByID("world"_spr)) {
            world->removeFromParent();
            return;
        }
        overlay->addChild(World::create());
    }

    bool World::init() {
        if (!CCNode::init()) return false;
        setID("world"_spr);
        auto gm = GameManager::get();
        m_icon = SimplePlayer::create(gm->getPlayerFrame());
        m_icon->setColor(gm->colorForIdx(gm->getPlayerColor()));
        m_icon->setSecondColor(gm->colorForIdx(gm->getPlayerColor2()));
        if (gm->getPlayerGlow()) m_icon->setGlowOutline(gm->colorForIdx(gm->getPlayerGlowColor()));
        m_icon->setScale(kHalf * 2.f / 30.f);
        
        m_body = CCNode::create();
        m_body->addChild(m_icon);
        m_body->ignoreAnchorPointForPosition(false);
        m_body->setPosition(getMousePos());
        addChild(m_body);

        for (size_t i = 0; i < kKeys.size(); i++) {
            m_keys[i] = KeyboardInputEvent(kKeys[i]).listen([this, i](KeyboardInputData& data) {
                if (data.action == KeyboardInputData::Action::Repeat) return ListenerResult::Propagate;
                auto typing = CCIMEDispatcher::sharedDispatcher()->hasDelegate();
                m_held[i] = data.action == KeyboardInputData::Action::Press && !typing;
                return ListenerResult::Propagate;
            });
        }
        m_mouse = MouseInputEvent().listen([this](MouseInputData& data) {
            if (data.button == MouseInputData::Button::Right)
                m_firing = data.action == MouseInputData::Action::Press;
            return ListenerResult::Propagate;
        });

        scheduleUpdate();

        return true;
    }

    void World::update(float dt) {
        auto inLevel = PlayLayer::get() || LevelEditorLayer::get();
        setVisible(!inLevel);
        auto scene = CCDirector::get()->getRunningScene();
        if (inLevel || !scene || typeinfo_cast<CCTransitionScene*>(scene)) return;
    }
}