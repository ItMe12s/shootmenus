#pragma once

#include <Geode/Geode.hpp>

namespace shootmenus {
    class World : public cocos2d::CCNode {
    public:
        static World* create();
        static void toggle();

    private:
        bool init() override;
        void update(float dt) override;

        geode::Ref<cocos2d::CCNode> m_body;
    };
}