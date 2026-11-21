#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/Keyboard.hpp>
#include <array>

namespace shootmenus {
    class World : public cocos2d::CCNode {
    public:
        static World* create();
        static void toggle();

    private:
        bool init() override;
        void update(float dt) override;

        geode::Ref<cocos2d::CCNode> m_body;
        geode::Ref<SimplePlayer> m_icon;
        std::array<bool, 4> m_held{};
        bool m_firing = false;
        std::array<geode::ListenerHandle, 4> m_keys;
        geode::ListenerHandle m_mouse;

        void moveCube(float dt);
        cocos2d::CCPoint m_velocity;
        bool m_grounded = false;
        float m_iconAngle = 0.f;
    };
}