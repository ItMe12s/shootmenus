#include <Geode/Geode.hpp>
#include "World.hpp"

using namespace geode::prelude;

$on_mod(Loaded) {
    listenForKeybindSettingPresses("spawn-cube", [](Keybind const&, bool down, bool repeat, double) {
        if (down && !repeat) shootmenus::World::toggle();
    });
}