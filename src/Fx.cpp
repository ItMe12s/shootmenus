#include "Util.hpp"
#include "World.hpp"

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace shootmenus {
    constexpr size_t kMaxBits = 400;
    constexpr float kAngleStep = 15.f;
    constexpr float kLightSteps = 4.f;

    constexpr char kHoleVert[] = R"(
attribute vec4 a_position;
attribute vec2 a_texCoord;
attribute vec4 a_color;
varying vec4 v_color;
varying vec2 v_uv;
varying vec2 v_local;

void main() {
    gl_Position = CC_MVPMatrix * a_position;
    v_color = a_color;
    v_uv = a_texCoord;
    v_local = a_position.xy;
}
)";

    constexpr char kHoleFrag[] = R"(
uniform sampler2D CC_Texture0;
uniform vec3 u_holes[16];
uniform float u_px;
varying vec4 v_color;
varying vec2 v_uv;
varying vec2 v_local;

void main() {
    vec4 c = v_color * texture2D(CC_Texture0, v_uv);
    vec2 cell = floor(v_local / u_px);
    vec2 p = (cell + 0.5) * u_px;
    float jag = mod(cell.x + cell.y, 2.0) * u_px;
    float band = 9.0;
    for (int i = 0; i < 16; i++) {
        vec3 h = u_holes[i];
        if (h.z <= 0.0) continue;
        band = min(band, (distance(p, h.xy) - h.z - jag) / u_px);
    }
    if (band < 0.0) discard;
    if (band < 1.0) c.rgb = vec3(1.0, 0.42, 0.08) * c.a;
    else if (band < 2.5) c.rgb *= 0.25;
    gl_FragColor = c;
}
)";

    Ref<CCGLProgram> World::holeProgram() {
        if (!m_spare.empty()) {
            auto program = m_spare.back();
            m_spare.pop_back();
            return program;
        }
        auto program = new CCGLProgram();
        program->autorelease();
        if (!program->initWithVertexShaderByteArray(kHoleVert, kHoleFrag)) return nullptr;
        program->addAttribute(kCCAttributeNamePosition, kCCVertexAttrib_Position);
        program->addAttribute(kCCAttributeNameColor, kCCVertexAttrib_Color);
        program->addAttribute(kCCAttributeNameTexCoord, kCCVertexAttrib_TexCoords);
        if (!program->link()) return nullptr;
        program->updateUniforms();
        return program;
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

    void World::addBit(char const* frame, Bit bit, CCPoint pos) {
        auto sprite = pixelSprite(frame);
        if (!sprite) return;
        bit.scale *= pixelScale();
        bit.growth *= pixelScale();
        sprite->setScale(bit.scale);
        sprite->setRotation(rnd(0.f, 360.f));
        addBit(sprite, bit, pos);
    }

    void World::addBit(CCSprite* sprite, Bit bit, CCPoint pos) {
        if (m_bits.size() >= kMaxBits) return;
        bit.sprite = sprite;
        bit.pos = pos;
        bit.angle = sprite->getRotation();
        sprite->setPosition(pos);
        sprite->setOpacity(bit.opacity);
        sprite->setColor(bit.color);
        if (bit.additive) sprite->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
        addChild(sprite, 6);
        m_bits.push_back(bit);
    }

    void World::updateBits(float dt) {
        std::erase_if(m_bits, [&](Bit& bit) {
            bit.life -= dt;
            if (bit.life <= 0.f) {
                bit.sprite->removeFromParent();
                return true;
            }
            auto pos = bit.pos;
            auto prevBottom = pos.y - 1.f;
            bit.velocity.y -= bit.gravity * dt;
            bit.velocity = bit.velocity * std::max(0.f, 1.f - bit.drag * dt);
            pos = pos + bit.velocity * dt;
            if (bit.bounce >= 0.f && land(pos, bit.velocity, prevBottom, 1.f, bit.bounce)) {
                bit.spin *= 0.7f;
                bit.velocity.x *= std::pow(0.05f, dt);
            }
            bit.pos = pos;
            if (bit.lit) {
                pos = CCPoint{
                    std::round(pos.x / kBitPixel) * kBitPixel, std::round(pos.y / kBitPixel) * kBitPixel
                };
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
                bit.angle += bit.spin * dt;
                bit.sprite->setRotation(
                    bit.lit ? std::round(bit.angle / kAngleStep) * kAngleStep : bit.angle
                );
                if (bit.growth != 0.f)
                    bit.sprite->setScale(bit.sprite->getScale() + bit.growth * dt);
            }
            if (bit.lit) {
                auto a = CC_DEGREES_TO_RADIANS(bit.angle);
                auto level =
                    std::round((0.72f + 0.3f * std::cos(a - 0.8f)) * kLightSteps) / kLightSteps;
                auto glint = std::abs(bit.spin) > 30.f && std::cos(a * 2.f - 1.3f) > 0.97f;
                auto shade = [&](GLubyte c) {
                    return glint ? GLubyte{255} : static_cast<GLubyte>(std::min(255.f, c * level));
                };
                bit.sprite->setColor({shade(bit.color.r), shade(bit.color.g), shade(bit.color.b)});
            }
            return false;
        });
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
} // namespace shootmenus
