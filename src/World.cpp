#include "World.hpp"

#include <Geode/ui/OverlayManager.hpp>
#include <Geode/utils/cocos.hpp>
#include <algorithm>
#include <cmath>

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

    static CCRect localRect(CCNode* node) {
        auto size = node->getContentSize();
        return {0.f, 0.f, size.width, size.height};
    }

    static CCRect intersect(CCRect const& a, CCRect const& b) {
        auto x = std::max(a.getMinX(), b.getMinX());
        auto y = std::max(a.getMinY(), b.getMinY());
        return {
            x,
            y,
            std::max(0.f, std::min(a.getMaxX(), b.getMaxX()) - x),
            std::max(0.f, std::min(a.getMaxY(), b.getMaxY()) - y)
        };
    }

    void World::collect(CCNode* node, CCRect const& clip) {
        for (auto child : node->getChildrenExt()) {
            if (!child->isVisible()) continue;
            auto box = intersect(
                CCRectApplyAffineTransform(localRect(child), child->nodeToWorldTransform()), clip
            );
            auto onScreen = box.size.width > 2.f && box.size.height > 2.f;
            if (onScreen && typeinfo_cast<CCMenuItem*>(child)) m_platforms.push_back({child, box});
            if (auto sprite = typeinfo_cast<CCSprite*>(child);
                onScreen && sprite && sprite->getOpacity() > 0)
                m_hittables.push_back({child, box});
            auto scroll = typeinfo_cast<CCScrollLayerExt*>(child);
            collect(child, scroll && scroll->m_cutContent ? box : clip);
        }
    }

    static bool overlaps(CCRect const& a, CCRect const& b) {
        constexpr float e = 0.01f;
        return a.getMinX() < b.getMaxX() - e && a.getMaxX() > b.getMinX() + e &&
            a.getMinY() < b.getMaxY() - e && a.getMaxY() > b.getMinY() + e;
    }

    void World::moveCube(float dt) {
        auto dir = static_cast<float>(m_held[kRight]) - static_cast<float>(m_held[kLeft]);
        m_velocity.x = approach(m_velocity.x, dir * kSpeed, (m_grounded ? 2400.f : 1100.f) * dt);
        if (m_held[kUp] && m_grounded) {
            m_velocity.y = kJump;
            m_grounded = false;
        }
        auto gravity =
            kGravity * (m_held[kDown] ? 2.5f : (!m_held[kUp] && m_velocity.y > 0.f ? 1.8f : 1.f));
        m_velocity.y = std::max(m_velocity.y - gravity * dt, -kMaxFall);

        auto win = CCDirector::get()->getWinSize();
        auto pos = m_body->getPosition();
        auto cube = [&] { return CCRect{pos.x - kHalf, pos.y - kHalf, kHalf * 2.f, kHalf * 2.f}; };

        auto dx = m_velocity.x * dt;
        pos.x += dx;
        for (auto const& p : m_platforms) {
            if (dx == 0.f || !overlaps(cube(), p.box)) continue;
            pos.x = dx > 0.f ? p.box.getMinX() - kHalf : p.box.getMaxX() + kHalf;
            m_velocity.x = 0.f;
        }
        if (pos.x < kHalf || pos.x > win.width - kHalf) {
            pos.x = std::clamp(pos.x, kHalf, win.width - kHalf);
            m_velocity.x = 0.f;
        }

        auto dy = m_velocity.y * dt;
        pos.y += dy;
        m_grounded = false;
        for (auto const& p : m_platforms) {
            if (dy == 0.f || !overlaps(cube(), p.box)) continue;
            if (dy < 0.f) {
                pos.y = p.box.getMaxY() + kHalf;
                m_grounded = true;
            }
            else pos.y = p.box.getMinY() - kHalf;
            m_velocity.y = 0.f;
        }
        if (pos.y <= kHalf) {
            pos.y = kHalf;
            m_velocity.y = 0.f;
            m_grounded = true;
        }
        pos.y = std::min(pos.y, win.height + 200.f);
        m_body->setPosition(pos);

        if (m_grounded)
            m_iconAngle = approach(m_iconAngle, std::round(m_iconAngle / 90.f) * 90.f, 720.f * dt);
        else m_iconAngle += (m_velocity.x < 0.f ? -1.f : 1.f) * 420.f * dt;
        m_icon->setRotation(m_iconAngle);
    }

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
        m_body->setPosition(getMousePos());
        addChild(m_body);

        for (size_t i = 0; i < kKeys.size(); i++) {
            m_keys[i] = KeyboardInputEvent(kKeys[i]).listen([this, i](KeyboardInputData& data) {
                if (data.action == KeyboardInputData::Action::Repeat)
                    return ListenerResult::Propagate;
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

        m_debug = CCDrawNode::create();
        addChild(m_debug, 100);
        return true;
    }

    void World::update(float dt) {
        auto inLevel = PlayLayer::get() || LevelEditorLayer::get();
        setVisible(!inLevel);
        auto scene = CCDirector::get()->getRunningScene();

        if (inLevel || !scene || typeinfo_cast<CCTransitionScene*>(scene)) return;
        m_platforms.clear();
        m_hittables.clear();
        auto win = CCDirector::get()->getWinSize();
        collect(scene, {0.f, 0.f, win.width, win.height});

        m_debug->clear();
        for (auto const& t : m_hittables)
            m_debug->drawRect(t.box, {0, 0, 0, 0}, 0.5f, {1, 0.2f, 0.2f, 0.8f});
        for (auto const& t : m_platforms)
            m_debug->drawRect(t.box, {0, 0, 0, 0}, 1.f, {0.2f, 1, 0.2f, 1});

        dt = std::min(dt, 1.f / 30.f);
        moveCube(dt);
    }
} // namespace shootmenus