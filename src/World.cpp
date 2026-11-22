#include "World.hpp"

#include "Util.hpp"

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

    constexpr float kHoleRadius = 2.f;
    constexpr float kAreaPerHit = 120.f;

    constexpr float kShardSize = 12.f;
    constexpr int kMaxShards = 6;

    constexpr float kFlashTime = 0.045f;

    constexpr float kShakeDecay = 3.f;
    constexpr float kShakeAmp = 2.f;
    constexpr float kShakeShot = 0.25f;
    constexpr float kShakeHit = 0.1f;
    constexpr float kShakeShatter = 0.5f;

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

    static CCGLProgram* baseProgram() {
        return CCShaderCache::sharedShaderCache()->programForKey(kCCShader_PositionTextureColor);
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
        shake(scene, dt);

        m_platforms.clear();
        m_hittables.clear();
        auto win = CCDirector::get()->getWinSize();
        auto base = scene->nodeToParentTransform();
        base.tx -= getPositionX();
        base.ty -= getPositionY();
        collect(scene, base, {0.f, 0.f, win.width, win.height});

        m_cube->move(dt, m_platforms);
        m_cube->aim(getMousePos(), dt);

        if (auto shot = m_cube->trigger(dt)) fire(*shot);
        updateBullets(dt);
        updateBits(dt);
        std::erase_if(m_damage, [&](Damage& d) {
            if (d.sprite->getParent()) return false;
            release(d);
            return true;
        });
    }

    void World::onExit() {
        if (m_shaken) m_shaken->setPosition(CCPointZero);
        for (auto& d : m_damage)
            release(d);
        m_damage.clear();
        CCNode::onExit();
    }

    void World::collect(CCNode* node, CCAffineTransform const& parent, CCRect const& clip) {
        for (auto child : node->getChildrenExt()) {
            if (!child->isVisible()) continue;
            auto world = CCAffineTransformConcat(child->nodeToParentTransform(), parent);
            auto box = intersect(CCRectApplyAffineTransform(localRect(child), world), clip);
            auto onScreen = box.size.width > 2.f && box.size.height > 2.f;
            if (onScreen && typeinfo_cast<CCMenuItem*>(child)) m_platforms.push_back(box);
            if (auto sprite = typeinfo_cast<CCSprite*>(child);
                onScreen && sprite && sprite->getOpacity() > 0)
                m_hittables.push_back({sprite, box, world});
            auto scroll = typeinfo_cast<CCScrollLayerExt*>(child);
            collect(child, world, scroll && scroll->m_cutContent ? box : clip);
        }
    }

    void World::shake(CCNode* scene, float dt) {
        if (m_shaken && m_shaken != scene) {
            m_shaken->setPosition(CCPointZero);
            m_shaken = nullptr;
        }
        if (m_shake <= 0.f) return;
        m_shake = std::max(0.f, m_shake - kShakeDecay * dt);
        auto amount = m_shake * m_shake * kShakeAmp;
        auto offset =
            m_shake > 0.f ? CCPoint{rnd(-amount, amount), rnd(-amount, amount)} : CCPointZero;
        scene->setPosition(offset);
        setPosition(offset);
        m_shaken = m_shake > 0.f ? scene : nullptr;
    }

    void World::fire(Cube::Shot const& shot) {
        auto end = shot.origin + shot.dir * kRange;
        Bullet bullet{shot.origin, shot.dir, kRange};
        auto best = 1.f;
        for (auto const& t : m_hittables) {
            auto span = rayRect(shot.origin - t.box.origin, end - shot.origin, t.box.size);
            if (!span) continue;
            auto toLocal = CCAffineTransformInvert(t.world);
            auto o = CCPointApplyAffineTransform(shot.origin, toLocal);
            if (localRect(t.sprite).containsPoint(o)) continue;
            auto e = CCPointApplyAffineTransform(end, toLocal);
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

        for (int i = 0; i < 2; i++) {
            addBit(
                "chip.png"_spr,
                {
                    .velocity = rotate(shot.dir, rnd(-25.f, 25.f)) * rnd(30.f, 90.f),
                    .life = rnd(0.4f, 0.7f),
                    .fade = 0.7f,
                    .gravity = -40.f,
                    .drag = 3.f,
                    .growth = 1.2f,
                    .opacity = 90,
                    .color = {190, 185, 175},
                },
                shot.origin
            );
        }

        if (auto flash = pixelSprite("flash.png"_spr)) {
            flash->setAnchorPoint({0.f, 0.5f});
            flash->setRotation(-CC_RADIANS_TO_DEGREES(std::atan2(shot.dir.y, shot.dir.x)));
            auto scale = pixelScale();
            flash->setScaleX(scale * rnd(0.7f, 1.1f));
            flash->setScaleY(scale * rnd(0.8f, 1.1f) * (utils::random::chance(0.5) ? -1.f : 1.f));
            addBit(flash, {.life = kFlashTime, .fade = kFlashTime, .additive = true}, shot.origin);
        }
        m_shake = std::max(m_shake, kShakeShot);
    }

    void World::updateBullets(float dt) {
        m_tracers->clear();
        std::erase_if(m_bullets, [&](Bullet& b) {
            auto step = std::min(kBulletSpeed * dt, b.left);
            b.pos = b.pos + b.dir * step;
            b.left -= step;
            b.traveled += step;
            auto tail = b.pos - b.dir * std::min(55.f, b.traveled);
            m_tracers->drawSegment(tail, b.pos, 1.8f, {1.f, 0.5f, 0.12f, 0.3f});
            m_tracers->drawSegment(tail, b.pos, 0.6f, {1.f, 0.95f, 0.75f, 0.9f});

            if (b.left > 0.f) return false;
            impact(b);
            return true;
        });
    }

    void World::impact(Bullet const& bullet) {
        auto sprite = bullet.target.data();
        if (!sprite || !sprite->getParent() || !sprite->isVisible()) return;
        auto hit = sprite->convertToWorldSpace(bullet.hitLocal);
        hitFx(hit, -bullet.dir);
        m_shake = std::max(m_shake, kShakeHit);

        auto t = sprite->nodeToWorldTransform();
        auto det = std::abs(t.a * t.d - t.b * t.c);
        auto hits = addHole(sprite, bullet.hitLocal, kBitPixel / std::sqrt(det)).hits;
        auto size = sprite->getContentSize();
        auto needed =
            std::clamp(static_cast<int>(size.width * size.height * det / kAreaPerHit), 2, kMaxHoles);
        if (hits >= needed) shatter(sprite, hit, bullet.dir);
    }

    World::Damage& World::addHole(CCSprite* sprite, CCPoint local, float px) {
        auto it = std::ranges::find(m_damage, sprite, [](Damage const& d) {
            return d.sprite.data();
        });
        if (it == m_damage.end()) {
            Damage damage{sprite};
            if (!sprite->getBatchNode() && sprite->getShaderProgram() == baseProgram()) {
                damage.program = holeProgram();
                if (damage.program) sprite->setShaderProgram(damage.program);
            }
            it = m_damage.insert(m_damage.end(), std::move(damage));
        }

        auto& damage = *it;
        auto slot = (damage.hits++ % kMaxHoles) * 3;
        if (!damage.program) return damage;
        damage.holes[slot] = local.x;
        damage.holes[slot + 1] = local.y;
        damage.holes[slot + 2] = kHoleRadius * px;
        auto program = damage.program.data();
        program->use();
        program->setUniformLocationWith3fv(
            program->getUniformLocationForName("u_holes"), damage.holes.data(), kMaxHoles
        );
        program->setUniformLocationWith1f(program->getUniformLocationForName("u_px"), px);
        return damage;
    }

    void World::release(Damage& damage) {
        if (!damage.program) return;
        if (damage.sprite->getShaderProgram() == damage.program)
            damage.sprite->setShaderProgram(baseProgram());
        m_spare.push_back(damage.program);
    }

    void World::shatter(CCSprite* sprite, CCPoint hit, CCPoint dir) {
        m_shake = std::min(1.f, m_shake + kShakeShatter);
        sprite->setVisible(false);
        if (auto item = typeinfo_cast<CCMenuItemSprite*>(sprite->getParent());
            item && item->getNormalImage() == sprite) {
            item->setVisible(false);
            item->setEnabled(false);
        }
        std::erase_if(m_damage, [&](Damage& d) {
            if (d.sprite.data() != sprite) return false;
            release(d);
            return true;
        });
        if (sprite->isTextureRectRotated()) return;

        auto rect = sprite->getTextureRect();
        auto t = sprite->nodeToWorldTransform();
        auto sx = std::hypot(t.a, t.b);
        auto sy = std::hypot(t.c, t.d);
        auto rotation = -CC_RADIANS_TO_DEGREES(std::atan2(t.b, t.a));
        auto cols = std::clamp(static_cast<int>(rect.size.width * sx / kShardSize), 1, kMaxShards);
        auto rows = std::clamp(static_cast<int>(rect.size.height * sy / kShardSize), 1, kMaxShards);
        CCSize cell{rect.size.width / cols, rect.size.height / rows};

        for (int j = 0; j < rows; j++) {
            for (int i = 0; i < cols; i++) {
                auto tx = sprite->isFlipX() ? cols - 1 - i : i;
                auto ty = sprite->isFlipY() ? j : rows - 1 - j;
                auto piece = CCSprite::createWithTexture(
                    sprite->getTexture(),
                    {rect.origin.x + tx * cell.width,
                     rect.origin.y + ty * cell.height,
                     cell.width,
                     cell.height}
                );
                if (!piece) continue;
                piece->setFlipX(sprite->isFlipX());
                piece->setFlipY(sprite->isFlipY());
                piece->setScaleX(sx);
                piece->setScaleY(sy);
                piece->setRotation(rotation);
                piece->setBlendFunc(sprite->getBlendFunc());
                auto pos = sprite->convertToWorldSpace(
                    sprite->getOffsetPosition() +
                    CCPoint{(i + 0.5f) * cell.width, (j + 0.5f) * cell.height}
                );
                auto away = pos - hit;
                auto falloff = 1.f / (1.f + away.getLength() / 40.f);
                addBit(
                    piece,
                    {
                        .velocity = dir * rnd(80.f, 260.f) * falloff +
                            away.normalize() * rnd(60.f, 180.f) * falloff +
                            CCPoint{rnd(-30.f, 30.f), rnd(30.f, 130.f)},
                        .spin = rnd(-400.f, 400.f),
                        .life = rnd(4.f, 6.f),
                        .fade = 1.f,
                        .gravity = 1300.f,
                        .drag = 0.25f,
                        .bounce = 0.3f,
                        .opacity = sprite->getDisplayedOpacity(),
                        .color = sprite->getDisplayedColor(),
                        .lit = true,
                    },
                    pos
                );
            }
        }
    }
} // namespace shootmenus
