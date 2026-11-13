/*
 *  resa-multiplayer (resam)
 *  Event System Implementation
 */

#include "event_manager.hpp"
#include "resource.hpp"
#include "resource_manager.hpp"

namespace Resa {

EventManager& EventManager::Get() {
    static EventManager instance;
    return instance;
}

void EventManager::AddListener(const std::string& eventName, Resource* resource, int luaRef) {
    listeners_[eventName].push_back({ resource, luaRef });
}

void EventManager::RemoveListenersForResource(Resource* resource) {
    for (auto& pair : listeners_) {
        auto& list = pair.second;
        for (auto it = list.begin(); it != list.end();) {
            if (it->resource == resource) {
                if (resource->GetLua()) {
                    luaL_unref(resource->GetLua(), LUA_REGISTRYINDEX, it->luaRef);
                }
                it = list.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void EventManager::TriggerEvent(const std::string& eventName, const std::vector<LuaValue>& args) {
    auto it = listeners_.find(eventName);
    if (it == listeners_.end()) return;

    for (const auto& listener : it->second) {
        if (!listener.resource || listener.resource->GetState() != ResourceState::Running) {
            continue;
        }

        lua_State* L = listener.resource->GetLua();
        if (!L) continue;

        lua_rawgeti(L, LUA_REGISTRYINDEX, listener.luaRef);
        if (!lua_isfunction(L, -1)) {
            lua_pop(L, 1);
            continue;
        }

        for (const auto& arg : args) {
            if (std::holds_alternative<bool>(arg)) {
                lua_pushboolean(L, std::get<bool>(arg));
            } else if (std::holds_alternative<int>(arg)) {
                lua_pushinteger(L, std::get<int>(arg));
            } else if (std::holds_alternative<double>(arg)) {
                lua_pushnumber(L, std::get<double>(arg));
            } else if (std::holds_alternative<std::string>(arg)) {
                lua_pushstring(L, std::get<std::string>(arg).c_str());
            } else {
                lua_pushnil(L);
            }
        }

        int status = lua_pcall(L, static_cast<int>(args.size()), 0, 0);
        if (status != LUA_OK) {
            const char* err = lua_tostring(L, -1);
            ResourceManager::Get().LogError(std::string("[Lua Error][") + listener.resource->GetName() + "] Event '" + eventName + "': " + (err ? err : "unknown error"));
            lua_pop(L, 1);
        }
    }
}

} // namespace Resa
