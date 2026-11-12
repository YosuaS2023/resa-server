/*
 *  resa-multiplayer (resam)
 *  Component Entry Point
 */

#include "resalua_component.hpp"

COMPONENT_ENTRY_POINT() {
    return new Resa::ResaLuaComponent();
}
