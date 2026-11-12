/*
 *  resa-multiplayer (resam)
 *  Lua Native Bindings
 */

#pragma once

#include "resalua.hpp"

namespace Resa {

void RegisterBindings(lua_State* L, Resource* resource);

} // namespace Resa
