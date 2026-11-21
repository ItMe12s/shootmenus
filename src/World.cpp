#include "World.hpp"

#include <Geode/ui/OverlayManager.hpp>
#include <Geode/utils/cocos.hpp>
#include <algorithm>

using namespace geode::prelude;

namespace shootmenus {
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
        overlay->addChild(World::create());
    }

    bool World::init() {
        if (!CCNode::init()) return false;
        setID("world"_spr);
        m_cube = Cube::create();
        m_cube->setPosition(getMousePos());
        addChild(m_cube);
        m_debug = CCDrawNode::create();
        addChild(m_debug, 100);
        scheduleUpdate();
        return true;
    }

    void World::update(float dt) {
        auto inLevel = PlayLayer::get() || LevelEditorLayer::get();
        setVisible(!inLevel);
        auto scene = CCDirector::get()->getRunningScene();
        if (inLevel || !scene || typeinfo_cast<CCTransitionScene*>(scene)) return;

        m_platforms.clear();
        m_hittables.clear();
        auto win = CCDirector::get()->getWinSize();
        collect(scene, {0.f, 0.f, win.width, win.height});

        m_debug->clear();
        for (auto const& t : m_hittables)
            m_debug->drawRect(t.box, {0, 0, 0, 0}, 0.5f, {1, 0.2f, 0.2f, 0.8f});
        for (auto const& box : m_platforms)
            m_debug->drawRect(box, {0, 0, 0, 0}, 1.f, {0.2f, 1, 0.2f, 1});

        m_cube->move(std::min(dt, 1.f / 30.f), m_platforms);
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
                m_hittables.push_back({child, box});
            auto scroll = typeinfo_cast<CCScrollLayerExt*>(child);
            collect(child, scroll && scroll->m_cutContent ? box : clip);
        }
    }
} // namespace shootmenus