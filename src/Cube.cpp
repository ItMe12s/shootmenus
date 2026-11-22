#include "Cube.hpp"

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace shootmenus {
    constexpr float kHalf = 12.f;
    constexpr float kSpeed = 240.f;
    constexpr float kGroundAccel = 2400.f;
    constexpr float kAirAccel = 1100.f;
    constexpr float kJump = 520.f;
    constexpr float kGravity = 1700.f;
    constexpr float kFastFall = 2.5f;
    constexpr float kShortHop = 1.8f;
    constexpr float kMaxFall = 900.f;
    constexpr float kCeiling = 200.f;
    constexpr float kSpin = 420.f;
    constexpr float kSnap = 720.f;
    constexpr float kGunLength = 62.f;
    constexpr CCPoint kGrip{0.350f, 0.633f};
    constexpr CCPoint kShoulder{7.f, -5.f};

    static float approach(float value, float target, float step) {
        return value < target ? std::min(value + step, target) : std::max(value - step, target);
    }

    static bool overlaps(CCRect const& a, CCRect const& b) {
        constexpr float e = 0.01f;
        return a.getMinX() < b.getMaxX() - e && a.getMaxX() > b.getMinX() + e &&
            a.getMinY() < b.getMaxY() - e && a.getMaxY() > b.getMinY() + e;
    }

    Cube* Cube::create() {
        auto ret = new Cube();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    bool Cube::init() {
        if (!CCNode::init()) return false;
        auto gm = GameManager::get();
        m_icon = SimplePlayer::create(gm->getPlayerFrame());
        m_icon->setColor(gm->colorForIdx(gm->getPlayerColor()));
        m_icon->setSecondColor(gm->colorForIdx(gm->getPlayerColor2()));
        if (gm->getPlayerGlow()) m_icon->setGlowOutline(gm->colorForIdx(gm->getPlayerGlowColor()));
        m_icon->setScale(kHalf * 2.f / 30.f);
        addChild(m_icon);

        m_gun = CCSprite::create("gun.png"_spr);
        if (!m_gun) return false;
        m_gun->getTexture()->setAliasTexParameters();
        m_gun->setAnchorPoint(kGrip);
        m_gun->setScale(kGunLength / m_gun->getContentWidth());

        m_pivot = CCNode::create();
        m_pivot->setPosition(kShoulder);
        m_pivot->addChild(m_gun);
        addChild(m_pivot, 1);

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
        return true;
    }

    void Cube::move(float dt, std::span<CCRect const> platforms) {
        auto dir = static_cast<float>(m_held[Right]) - static_cast<float>(m_held[Left]);
        m_velocity.x =
            approach(m_velocity.x, dir * kSpeed, (m_grounded ? kGroundAccel : kAirAccel) * dt);
        if (m_held[Up] && m_grounded) {
            m_velocity.y = kJump;
            m_grounded = false;
        }
        auto gravity = kGravity *
            (m_held[Down] ? kFastFall : (!m_held[Up] && m_velocity.y > 0.f ? kShortHop : 1.f));
        m_velocity.y = std::max(m_velocity.y - gravity * dt, -kMaxFall);

        auto win = CCDirector::get()->getWinSize();
        auto pos = getPosition();
        auto box = [&] {
            return CCRect{pos.x - kHalf, pos.y - kHalf, kHalf * 2.f, kHalf * 2.f};
        };

        auto dx = m_velocity.x * dt;
        pos.x += dx;
        for (auto const& p : platforms) {
            if (dx == 0.f || !overlaps(box(), p)) continue;
            pos.x = dx > 0.f ? p.getMinX() - kHalf : p.getMaxX() + kHalf;
            m_velocity.x = 0.f;
        }
        if (pos.x < kHalf || pos.x > win.width - kHalf) {
            pos.x = std::clamp(pos.x, kHalf, win.width - kHalf);
            m_velocity.x = 0.f;
        }

        auto dy = m_velocity.y * dt;
        pos.y += dy;
        m_grounded = false;
        for (auto const& p : platforms) {
            if (dy == 0.f || !overlaps(box(), p)) continue;
            if (dy < 0.f) {
                pos.y = p.getMaxY() + kHalf;
                m_grounded = true;
            }
            else pos.y = p.getMinY() - kHalf;
            m_velocity.y = 0.f;
        }
        if (pos.y <= kHalf) {
            pos.y = kHalf;
            m_velocity.y = 0.f;
            m_grounded = true;
        }
        pos.y = std::min(pos.y, win.height + kCeiling);
        setPosition(pos);

        if (m_grounded)
            m_iconAngle = approach(m_iconAngle, std::round(m_iconAngle / 90.f) * 90.f, kSnap * dt);
        else m_iconAngle += (m_velocity.x < 0.f ? -1.f : 1.f) * kSpin * dt;
        m_icon->setRotation(m_iconAngle);
    }

    void Cube::aim(CCPoint target) {
        auto local = convertToNodeSpace(target);
        auto left = local.x < 0.f;
        m_pivot->setPosition({left ? -kShoulder.x : kShoulder.x, kShoulder.y});
        auto delta = local - m_pivot->getPosition();
        m_pivot->setRotation(-CC_RADIANS_TO_DEGREES(std::atan2(delta.y, delta.x)));
        m_gun->setScaleY(std::abs(m_gun->getScaleY()) * (left ? -1.f : 1.f));
        m_icon->setScaleX(std::abs(m_icon->getScaleX()) * (left ? -1.f : 1.f));
    }
} // namespace shootmenus