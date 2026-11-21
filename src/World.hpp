#pragma once

#include "Cube.hpp"

#include <Geode/Geode.hpp>
#include <vector>

namespace shootmenus {
    class World : public cocos2d::CCNode {
    public:
        static World* create();
        static void toggle();

    private:
        struct Target {
            cocos2d::CCNode* node;
            cocos2d::CCRect box;
        };

        bool init() override;
        void update(float dt) override;
        void collect(cocos2d::CCNode* node, cocos2d::CCRect const& clip);

        geode::Ref<Cube> m_cube;
        std::vector<cocos2d::CCRect> m_platforms;
        std::vector<Target> m_hittables;
        geode::Ref<cocos2d::CCDrawNode> m_debug;
    };
} // namespace shootmenus