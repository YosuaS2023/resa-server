/*
 *  resa-multiplayer (resam)
 *  Lua <-> LuaMySQL Component Bridge
 */

#pragma once

#include "lua_defs.hpp"

namespace Resa {

void RegisterMySQLBindings(lua_State* L, Resource* resource);
void CleanupMySQLForResource(Resource* resource);

} // namespace Resa
