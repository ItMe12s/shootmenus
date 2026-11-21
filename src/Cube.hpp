#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/Keyboard.hpp>
#include <array>
#include <span>

namespace shootmenus {
    class Cube : public cocos2d::CCNode {
    public:
        static Cube* create();
        void move(float dt, std::span<cocos2d::CCRect const> platforms);

    private:
        enum Key : size_t {
            Left,
            Right,
            Up,
            Down
        };

        static constexpr std::array<cocos2d::enumKeyCodes, 4>
            kKeys{cocos2d::KEY_A, cocos2d::KEY_D, cocos2d::KEY_W, cocos2d::KEY_S};

        bool init() override;

        geode::Ref<SimplePlayer> m_icon;
        cocos2d::CCPoint m_velocity;
        bool m_grounded = false;
        float m_iconAngle = 0.f;

        std::array<bool, kKeys.size()> m_held{};
        bool m_firing = false;
        std::array<geode::ListenerHandle, kKeys.size()> m_keys;
        geode::ListenerHandle m_mouse;

        void aim(cocos2d::CCPoint target);

        geode::Ref<cocos2d::CCNode> m_pivot;
        geode::Ref<cocos2d::CCSprite> m_gun;
    };
} // namespace shootmenus