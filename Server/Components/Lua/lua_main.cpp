/*
 *  resa-multiplayer (resam)
 *  Component Entry Point
 */

#include "lua_component.hpp"

COMPONENT_ENTRY_POINT() {
    return new Resa::LuaComponent();
}
