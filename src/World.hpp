#pragma once

#include "Cube.hpp"

#include <Geode/Geode.hpp>
#include <array>
#include <vector>

namespace shootmenus {
    class World : public cocos2d::CCNode {
    public:
        CREATE_FUNC(World);
        static void toggle();

    private:
        struct Target {
            cocos2d::CCSprite* sprite;
            cocos2d::CCRect box;
            cocos2d::CCAffineTransform world;
        };

        struct Bullet {
            cocos2d::CCPoint pos;
            cocos2d::CCPoint dir;
            float left = 0.f;
            float traveled = 0.f;
            geode::Ref<cocos2d::CCSprite> target;
            cocos2d::CCPoint hitLocal;
        };

        struct Bit {
            geode::Ref<cocos2d::CCSprite> sprite;
            cocos2d::CCPoint pos;
            float angle = 0.f;
            cocos2d::CCPoint velocity;
            float spin = 0.f;
            float life = 1.f;
            float fade = 1.f;
            float gravity = 0.f;
            float drag = 0.f;
            float bounce = -1.f;
            float growth = 0.f;
            float scale = 1.f;
            GLubyte opacity = 255;
            cocos2d::ccColor3B color{255, 255, 255};
            bool streak = false;
            bool additive = false;
            bool lit = false;
        };

        static constexpr int kMaxHoles = 16;

        struct Damage {
            geode::Ref<cocos2d::CCSprite> sprite;
            geode::Ref<cocos2d::CCGLProgram> program;
            std::array<float, kMaxHoles * 3> holes{};
            int hits = 0;
        };

        bool init() override;
        void update(float dt) override;
        void onExit() override;

        void collect(
            cocos2d::CCNode* node, cocos2d::CCAffineTransform const& parent, cocos2d::CCRect const& clip
        );
        void shake(cocos2d::CCNode* scene, float dt);
        void fire(Cube::Shot const& shot);
        void updateBullets(float dt);
        void impact(Bullet const& bullet);
        Damage& addHole(cocos2d::CCSprite* sprite, cocos2d::CCPoint local, float px);
        void release(Damage& damage);
        geode::Ref<cocos2d::CCGLProgram> holeProgram();
        void shatter(cocos2d::CCSprite* sprite, cocos2d::CCPoint hit, cocos2d::CCPoint dir);

        void hitFx(cocos2d::CCPoint pos, cocos2d::CCPoint normal);
        void addBit(char const* frame, Bit bit, cocos2d::CCPoint pos);
        void addBit(cocos2d::CCSprite* sprite, Bit bit, cocos2d::CCPoint pos);
        void updateBits(float dt);
        bool land(
            cocos2d::CCPoint& pos, cocos2d::CCPoint& vel, float prevBottom, float radius, float bounce
        );

        geode::Ref<Cube> m_cube;
        std::vector<cocos2d::CCRect> m_platforms;
        std::vector<Target> m_hittables;

        std::vector<Bullet> m_bullets;
        geode::Ref<cocos2d::CCDrawNode> m_tracers;

        std::vector<Damage> m_damage;
        std::vector<geode::Ref<cocos2d::CCGLProgram>> m_spare;
        std::vector<Bit> m_bits;

        float m_shake = 0.f;
        geode::Ref<cocos2d::CCNode> m_shaken;
    };
} // namespace shootmenus
