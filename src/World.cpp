#include "World.hpp"

#include <Geode/ui/OverlayManager.hpp>

using namespace geode::prelude;

namespace shootmenus {
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
        m_body = CCLayerColor::create({255, 255, 255, 255}, 24.f, 24.f);
        m_body->ignoreAnchorPointForPosition(false);
        m_body->setPosition(getMousePos());
        addChild(m_body);
        scheduleUpdate();
        return true;
    }

    void World::update(float dt) {
        auto inLevel = PlayLayer::get() || LevelEditorLayer::get();
        setVisible(!inLevel);
        auto scene = CCDirector::get()->getRunningScene();
        if (inLevel || !scene || typeinfo_cast<CCTransitionScene*>(scene)) return;
    }
}