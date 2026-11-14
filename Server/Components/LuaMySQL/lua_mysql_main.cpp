/*
 *  resa-multiplayer (resam)
 *  LuaMySQL Component Entry Point
 */

#include "lua_mysql_component.hpp"

COMPONENT_ENTRY_POINT() {
    return new Resa::LuaMySQLComponent();
}
