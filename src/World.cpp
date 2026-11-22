#include "World.hpp"

#include <Geode/ui/OverlayManager.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/random.hpp>
#include <algorithm>
#include <cmath>
#include <optional>
#include <ranges>
#include <unordered_map>
#include <utility>

using namespace geode::prelude;

namespace shootmenus {
    constexpr float kRange = 1500.f;
    constexpr float kBulletSpeed = 3200.f;
    constexpr size_t kMaxBits = 400;
    constexpr size_t kMaxHoles = 300;
    constexpr float kBitPixel = 1.f;
    constexpr float kAreaPerHit = 120.f;
    constexpr float kShakeDecay = 5.f;
    constexpr float kShakeAmp = 4.f;
    constexpr float kShakeShot = 0.25f;
    constexpr float kShakeHit = 0.06f;
    constexpr float kShakeShatter = 0.6f;
    constexpr float kFlashTime = 0.045f;

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

    static CCSprite* pixelSprite(char const* frame) {
        auto sprite = CCSprite::create(frame);
        if (sprite) sprite->getTexture()->setAliasTexParameters();
        return sprite;
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
        shake(scene, dt);

        m_platforms.clear();
        m_hittables.clear();
        auto win = CCDirector::get()->getWinSize();
        collect(scene, scene->nodeToParentTransform(), {0.f, 0.f, win.width, win.height});

        m_cube->move(dt, m_platforms);
        m_cube->aim(getMousePos(), dt);

        if (auto shot = m_cube->trigger(dt)) fire(*shot);
        updateBullets(dt);
        updateBits(dt);
        updateHoles();
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
            auto scale = CC_CONTENT_SCALE_FACTOR() * kBitPixel;
            flash->setScaleX(scale * rnd(0.7f, 1.1f));
            flash->setScaleY(scale * rnd(0.8f, 1.1f) * (rnd(0.f, 1.f) < 0.5f ? -1.f : 1.f));
            addBit(flash, {.life = kFlashTime, .fade = kFlashTime, .additive = true}, shot.origin);
        }
        m_shake = std::min(1.f, m_shake + kShakeShot);
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
        addHole(sprite, bullet.hitLocal);
        m_shake = std::min(1.f, m_shake + kShakeHit);

        auto t = sprite->nodeToWorldTransform();
        auto size = sprite->getContentSize();
        auto area = size.width * size.height * std::abs(t.a * t.d - t.b * t.c);
        auto needed = std::clamp(static_cast<int>(area / kAreaPerHit), 2, 16);
        auto hits = std::ranges::count_if(m_holes, [&](Hole const& h) {
            return h.target.data() == sprite;
        });
        if (hits >= needed) shatter(sprite, hit, bullet.dir);
    }

    void World::addBit(char const* frame, Bit bit, CCPoint pos) {
        auto sprite = pixelSprite(frame);
        if (!sprite) return;
        bit.scale *= CC_CONTENT_SCALE_FACTOR() * kBitPixel;
        bit.growth *= CC_CONTENT_SCALE_FACTOR() * kBitPixel;
        sprite->setScale(bit.scale);
        sprite->setRotation(rnd(0.f, 360.f));
        addBit(sprite, bit, pos);
    }

    void World::addBit(CCSprite* sprite, Bit bit, CCPoint pos) {
        if (m_bits.size() >= kMaxBits) return;
        bit.sprite = sprite;
        sprite->setPosition(pos);
        sprite->setOpacity(bit.opacity);
        sprite->setColor(bit.color);
        if (bit.additive) sprite->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
        addChild(sprite, 6);
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
                if (bit.growth != 0.f)
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

    void World::addHole(CCSprite* target, CCPoint local) {
        auto sprite = pixelSprite("hole.png"_spr);
        if (!sprite) return;
        if (m_holes.size() >= kMaxHoles) {
            m_holes.front().sprite->removeFromParent();
            m_holes.erase(m_holes.begin());
        }
        sprite->setScale(CC_CONTENT_SCALE_FACTOR() * kBitPixel);
        sprite->setRotation(90.f * static_cast<int>(rnd(0.f, 4.f)));
        addChild(sprite, 3);
        m_holes.push_back({sprite, target, local});
    }

    void World::updateHoles() {
        std::unordered_map<CCSprite*, Target const*> targets;
        for (auto const& t : m_hittables)
            targets.emplace(t.sprite, &t);
        std::erase_if(m_holes, [&](Hole& hole) {
            if (!hole.target->getParent()) {
                hole.sprite->removeFromParent();
                return true;
            }
            auto it = targets.find(hole.target.data());
            hole.sprite->setVisible(false);
            if (it == targets.end()) return false;
            auto pos = CCPointApplyAffineTransform(hole.local, it->second->world);
            hole.sprite->setPosition(pos - getPosition());
            hole.sprite->setVisible(it->second->box.containsPoint(pos));
            return false;
        });
    }

    void World::shatter(CCSprite* sprite, CCPoint hit, CCPoint dir) {
        m_shake = std::min(1.f, m_shake + kShakeShatter);
        sprite->setVisible(false);
        if (auto item = typeinfo_cast<CCMenuItemSprite*>(sprite->getParent());
            item && item->getNormalImage() == sprite) {
            item->setVisible(false);
            item->setEnabled(false);
        }
        std::erase_if(m_holes, [&](Hole& h) {
            if (h.target.data() != sprite) return false;
            h.sprite->removeFromParent();
            return true;
        });
        if (sprite->isTextureRectRotated()) return;

        auto rect = sprite->getTextureRect();
        auto t = sprite->nodeToWorldTransform();
        auto sx = std::hypot(t.a, t.b);
        auto sy = std::hypot(t.c, t.d);
        auto rotation = -CC_RADIANS_TO_DEGREES(std::atan2(t.b, t.a));
        auto cols = std::clamp(static_cast<int>(rect.size.width * sx / 12.f), 1, 6);
        auto rows = std::clamp(static_cast<int>(rect.size.height * sy / 12.f), 1, 6);
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
                    },
                    pos
                );
            }
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

    void World::onExit() {
        if (m_shaken) m_shaken->setPosition(CCPointZero);
        CCNode::onExit();
    }
} // namespace shootmenus
