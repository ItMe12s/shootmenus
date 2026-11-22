#include "World.hpp"

#include <Geode/ui/OverlayManager.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/random.hpp>
#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

using namespace geode::prelude;

namespace shootmenus {
    constexpr float kRange = 1500.f;
    constexpr float kBulletSpeed = 3200.f;
    constexpr size_t kMaxBits = 400;

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

    static float rnd(float min, float max) {
        return utils::random::generate(min, max);
    }

    static CCPoint rotate(CCPoint v, float degrees) {
        return v.rotateByAngle(CCPointZero, CC_DEGREES_TO_RADIANS(degrees));
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
        if (auto world = World::create()) overlay->addChild(world);
    }

    bool World::init() {
        if (!CCNode::init()) return false;
        setID("world"_spr);

        m_cube = Cube::create();
        if (!m_cube) return false;
        m_cube->setPosition(getMousePos());
        addChild(m_cube);

        m_tracers = CCDrawNode::create();
        m_tracers->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
        addChild(m_tracers, 5);

        scheduleUpdate();
        return true;
    }

    void World::update(float dt) {
        auto inLevel = PlayLayer::get() || LevelEditorLayer::get();
        setVisible(!inLevel);
        auto scene = CCDirector::get()->getRunningScene();
        if (inLevel || !scene || typeinfo_cast<CCTransitionScene*>(scene)) return;
        dt = std::min(dt, 1.f / 30.f);

        m_platforms.clear();
        m_hittables.clear();
        auto win = CCDirector::get()->getWinSize();
        collect(scene, {0.f, 0.f, win.width, win.height});

        m_cube->move(dt, m_platforms);
        m_cube->aim(getMousePos(), dt);

        if (auto shot = m_cube->trigger(dt)) fire(*shot);
        updateBullets(dt);
        updateBits(dt);
    }

    void World::collect(CCNode* node, CCRect const& clip) {
        for (auto child : node->getChildrenExt()) {
            if (!child->isVisible()) continue;
            auto box = intersect(
                CCRectApplyAffineTransform(localRect(child), child->nodeToWorldTransform()), clip
            );
            auto onScreen = box.size.width > 2.f && box.size.height > 2.f;
            if (onScreen && typeinfo_cast<CCMenuItem*>(child)) m_platforms.push_back(box);
            if (auto sprite = typeinfo_cast<CCSprite*>(child);
                onScreen && sprite && sprite->getOpacity() > 0)
                m_hittables.push_back({sprite, box});
            auto scroll = typeinfo_cast<CCScrollLayerExt*>(child);
            collect(child, scroll && scroll->m_cutContent ? box : clip);
        }
    }

    static std::optional<std::pair<float, float>> rayRect(
        CCPoint o, CCPoint d, CCSize size, float tmin = 0.f, float tmax = 1.f
    ) {
        auto slab = [&](float orig, float delta, float extent) {
            if (std::abs(delta) < 1e-6f) return orig >= 0.f && orig <= extent;
            auto t1 = -orig / delta;
            auto t2 = (extent - orig) / delta;
            tmin = std::max(tmin, std::min(t1, t2));
            tmax = std::min(tmax, std::max(t1, t2));
            return tmin <= tmax;
        };

        if (!slab(o.x, d.x, size.width) || !slab(o.y, d.y, size.height)) return std::nullopt;
        return std::pair{tmin, tmax};
    }

    void World::fire(Cube::Shot const& shot) {
        auto end = shot.origin + shot.dir * kRange;
        Bullet bullet{shot.origin, shot.dir, kRange};
        auto best = 1.f;
        for (auto const& t : m_hittables) {
            auto o = t.sprite->convertToNodeSpace(shot.origin);
            if (localRect(t.sprite).containsPoint(o)) continue;
            auto span = rayRect(shot.origin - t.box.origin, end - shot.origin, t.box.size);
            if (!span) continue;
            auto e = t.sprite->convertToNodeSpace(end);
            auto hit = rayRect(o, e - o, t.sprite->getContentSize(), span->first, span->second);
            if (!hit || hit->first >= best) continue;
            best = hit->first;
            bullet.left = best * kRange;
            bullet.target = t.sprite;
            bullet.hitLocal = o + (e - o) * best;
        }
        m_bullets.push_back(bullet);

        addBit(
            "casing.png"_spr,
            {
                .velocity = shot.up * rnd(140.f, 220.f) - shot.dir * rnd(20.f, 70.f),
                .spin = rnd(-1400.f, 1400.f),
                .life = rnd(2.5f, 3.5f),
                .fade = 0.5f,
                .gravity = 1300.f,
                .drag = 0.3f,
                .bounce = 0.45f,
            },
            shot.port
        );
    }

    void World::updateBullets(float dt) {
        m_tracers->clear();
        std::vector<Bullet> done;
        std::erase_if(m_bullets, [&](Bullet& b) {
            auto step = std::min(kBulletSpeed * dt, b.left);
            b.pos = b.pos + b.dir * step;
            b.left -= step;
            b.traveled += step;
            auto tail = b.pos - b.dir * std::min(55.f, b.traveled);
            m_tracers->drawSegment(tail, b.pos, 1.8f, {1.f, 0.5f, 0.12f, 0.3f});
            m_tracers->drawSegment(tail, b.pos, 0.6f, {1.f, 0.95f, 0.75f, 0.9f});

            if (b.left > 0.f) return false;
            done.push_back(b);
            return true;
        });
        for (auto const& b : done)
            impact(b);
    }

    void World::impact(Bullet const& bullet) {
        auto sprite = bullet.target.data();
        if (!sprite || !sprite->getParent()) return;
        hitFx(sprite->convertToWorldSpace(bullet.hitLocal), -bullet.dir);
    }

    void World::addBit(char const* frame, Bit bit, CCPoint pos) {
        if (m_bits.size() >= kMaxBits) return;
        bit.sprite = CCSprite::create(frame);

        if (!bit.sprite) return;
        bit.scale *= CC_CONTENT_SCALE_FACTOR();
        bit.growth *= CC_CONTENT_SCALE_FACTOR();

        bit.sprite->getTexture()->setAliasTexParameters();
        bit.sprite->setPosition(pos);
        bit.sprite->setScale(bit.scale);
        bit.sprite->setRotation(rnd(0.f, 360.f));
        bit.sprite->setOpacity(bit.opacity);
        bit.sprite->setColor(bit.color);

        if (bit.additive) bit.sprite->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
        addChild(bit.sprite, 6);
        m_bits.push_back(bit);
    }

    bool World::land(CCPoint& pos, CCPoint& vel, float prevBottom, float radius, float bounce) {
        auto ground = 0.f;
        for (auto const& box : m_platforms) {
            if (pos.x > box.getMinX() && pos.x < box.getMaxX() && prevBottom >= box.getMaxY() - 0.5f)
                ground = std::max(ground, box.getMaxY());
        }

        auto win = CCDirector::get()->getWinSize();
        if (pos.x < 0.f || pos.x > win.width) {
            pos.x = std::clamp(pos.x, 0.f, win.width);
            vel.x = -vel.x * bounce;
        }

        if (pos.y - radius >= ground) return false;
        pos.y = ground + radius;
        vel.y = vel.y < -60.f ? -vel.y * bounce : 0.f;
        vel.x *= 0.85f;

        return true;
    }

    void World::updateBits(float dt) {
        std::erase_if(m_bits, [&](Bit& bit) {
            bit.life -= dt;
            if (bit.life <= 0.f) {
                bit.sprite->removeFromParent();
                return true;
            }
            auto pos = bit.sprite->getPosition();
            auto prevBottom = pos.y - 1.f;
            bit.velocity.y -= bit.gravity * dt;
            bit.velocity = bit.velocity * std::max(0.f, 1.f - bit.drag * dt);
            pos = pos + bit.velocity * dt;
            if (bit.bounce >= 0.f && land(pos, bit.velocity, prevBottom, 1.f, bit.bounce)) {
                bit.spin *= 0.7f;
                bit.velocity.x *= std::pow(0.05f, dt);
            }
            bit.sprite->setPosition(pos);
            bit.sprite->setOpacity(
                static_cast<GLubyte>(bit.opacity * std::min(1.f, bit.life / bit.fade))
            );
            if (bit.streak) {
                bit.sprite->setRotation(
                    -CC_RADIANS_TO_DEGREES(std::atan2(bit.velocity.y, bit.velocity.x))
                );
                bit.sprite->setScaleX(bit.scale * (1.f + bit.velocity.getLength() / 120.f));
                bit.sprite->setScaleY(bit.scale * 0.6f);
            }
            else {
                bit.sprite->setRotation(bit.sprite->getRotation() + bit.spin * dt);
                bit.sprite->setScale(bit.sprite->getScale() + bit.growth * dt);
            }
            return false;
        });
    }

    void World::hitFx(CCPoint pos, CCPoint normal) {
        for (int i = 0, n = static_cast<int>(rnd(8.f, 15.f)); i < n; i++) {
            auto heat = rnd(0.f, 1.f);
            addBit(
                "spark.png"_spr,
                {
                    .velocity = rotate(normal, rnd(-75.f, 75.f)) * rnd(150.f, 480.f),
                    .life = rnd(0.15f, 0.4f),
                    .fade = 0.4f,
                    .gravity = 900.f,
                    .drag = 2.f,
                    .bounce = 0.4f,
                    .color =
                        {255,
                         static_cast<GLubyte>(150 + heat * 90),
                         static_cast<GLubyte>(40 + heat * 110)},
                    .streak = true,
                    .additive = true,
                },
                pos
            );
        }
        for (int i = 0, n = static_cast<int>(rnd(4.f, 8.f)); i < n; i++) {
            auto shade = rnd(90.f, 200.f);
            addBit(
                "chip.png"_spr,
                {
                    .velocity =
                        rotate(normal, rnd(-60.f, 60.f)) * rnd(80.f, 230.f) + CCPoint{0.f, 60.f},
                    .spin = rnd(-720.f, 720.f),
                    .life = rnd(1.5f, 2.5f),
                    .fade = 0.5f,
                    .gravity = 1300.f,
                    .drag = 0.3f,
                    .bounce = 0.35f,
                    .color =
                        {static_cast<GLubyte>(shade),
                         static_cast<GLubyte>(shade * 0.92f),
                         static_cast<GLubyte>(shade * 0.8f)},
                },
                pos
            );
        }
    }
} // namespace shootmenus