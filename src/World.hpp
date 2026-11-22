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
            cocos2d::CCSprite* sprite;
            cocos2d::CCRect box;
        };

        struct Bullet {
            cocos2d::CCPoint pos;
            cocos2d::CCPoint dir;
            float left = 0.f;
            float traveled = 0.f;
            geode::Ref<cocos2d::CCSprite> target;
            cocos2d::CCPoint hitLocal;
        };

        bool init() override;
        void update(float dt) override;
        void collect(cocos2d::CCNode* node, cocos2d::CCRect const& clip);
        void fire(Cube::Shot const& shot);
        void updateBullets(float dt);
        void impact(Bullet const& bullet);

        geode::Ref<Cube> m_cube;
        std::vector<cocos2d::CCRect> m_platforms;
        std::vector<Target> m_hittables;
        std::vector<Bullet> m_bullets;
        geode::Ref<cocos2d::CCDrawNode> m_tracers;
    };
} // namespace shootmenus