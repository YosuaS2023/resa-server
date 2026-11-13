/*
 *  resa-multiplayer (resam)
 *  Event System
 */

#pragma once

#include "lua_defs.hpp"
#include <vector>
#include <string>
#include <unordered_map>
#include <variant>

namespace Resa {

using LuaValue = std::variant<std::monostate, bool, double, int, std::string>;

struct EventListener {
    Resource* resource;
    int luaRef; // reference in resource's lua_State (LUA_REGISTRYINDEX)
};

class EventManager {
public:
    static EventManager& Get();

    void AddListener(const std::string& eventName, Resource* resource, int luaRef);
    void RemoveListenersForResource(Resource* resource);

    // Trigger local event across all running resources
    void TriggerEvent(const std::string& eventName, const std::vector<LuaValue>& args);

private:
    std::unordered_map<std::string, std::vector<EventListener>> listeners_;
};

} // namespace Resa
