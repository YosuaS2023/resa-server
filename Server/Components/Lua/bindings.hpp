/*
 *  resa-multiplayer (resam)
 *  Lua Native Bindings
 */

#pragma once

#include "lua_defs.hpp"

namespace Resa {

void RegisterBindings(lua_State* L, Resource* resource);
bool DispatchCommand(int playerId, const std::string& fullCommand);
void RemoveCommandsForResource(Resource* resource);
void KillTimersForResource(Resource* resource);

} // namespace Resa
