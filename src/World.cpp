#include "World.hpp"

#include <Geode/ui/OverlayManager.hpp>
#include <Geode/utils/cocos.hpp>
#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

using namespace geode::prelude;

namespace shootmenus {
    constexpr float kRange = 1500.f;
    constexpr float kBulletSpeed = 3200.f;

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
        sprite->setColor({255, 80, 80});
    }
} // namespace shootmenus