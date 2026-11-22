#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/random.hpp>

namespace shootmenus {
    inline constexpr float kBitPixel = 1.f;

    inline float pixelScale() {
        return CC_CONTENT_SCALE_FACTOR() * kBitPixel;
    }

    inline float rnd(float min, float max) {
        return geode::utils::random::generate(min, max);
    }

    inline cocos2d::CCPoint rotate(cocos2d::CCPoint v, float degrees) {
        return v.rotateByAngle(cocos2d::CCPointZero, CC_DEGREES_TO_RADIANS(degrees));
    }

    inline cocos2d::CCSprite* pixelSprite(char const* frame) {
        auto sprite = cocos2d::CCSprite::create(frame);
        if (sprite) sprite->getTexture()->setAliasTexParameters();
        return sprite;
    }
} // namespace shootmenus
