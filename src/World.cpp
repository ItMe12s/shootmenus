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

    constexpr float kSpeed = 240.f;
    constexpr float kJump = 520.f;
    constexpr float kGravity = 1700.f;
    constexpr float kMaxFall = 900.f;

    static float approach(float value, float target, float step) {
        return value < target ? std::min(value + step, target) : std::max(value - step, target);
    }

    void World::moveCube(float dt) {
        // Kinda like super mario movement
        auto dir = static_cast<float>(m_held[kRight]) - static_cast<float>(m_held[kLeft]);
        m_velocity.x = approach(m_velocity.x, dir * kSpeed, (m_grounded ? 2400.f : 1100.f) * dt);
        if (m_held[kUp] && m_grounded) {
            m_velocity.y = kJump;
            m_grounded = false;
        }
        auto gravity = kGravity * (m_held[kDown] ? 2.5f : (!m_held[kUp] && m_velocity.y > 0.f ? 1.8f : 1.f));
        m_velocity.y = std::max(m_velocity.y - gravity * dt, -kMaxFall);

        auto win = CCDirector::get()->getWinSize();
        auto pos = m_body->getPosition();
        
        pos.x += m_velocity.x * dt;
        if (pos.x < kHalf || pos.x > win.width - kHalf) {
            pos.x = std::clamp(pos.x, kHalf, win.width - kHalf);
            m_velocity.x = 0.f;
        }

        pos.y += m_velocity.y * dt;
        m_grounded = false;
        if (pos.y <= kHalf) {
            pos.y = kHalf;
            m_velocity.y = 0.f;
            m_grounded = true;
        }
        pos.y = std::min(pos.y, win.height + 200.f);
        m_body->setPosition(pos);

        // Snap icon
        if (m_grounded) m_iconAngle = approach(m_iconAngle, std::round(m_iconAngle / 90.f) * 90.f, 720.f * dt);
        else m_iconAngle += (m_velocity.x < 0.f ? -1.f : 1.f) * 420.f * dt;
        m_icon->setRotation(m_iconAngle);
    }

    // Init show/hide
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

    // Main
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
        dt = std::min(dt, 1.f / 30.f);
        moveCube(dt);
    }
}