/*
 *  resa-multiplayer (resam)
 *  Resource Implementation
 */

#include "resource.hpp"
#include "resource_manager.hpp"
#include "event_manager.hpp"
#include "bindings.hpp"
#include <ghc/filesystem.hpp>
#include <fstream>
#include <iostream>

namespace Resa {

namespace fs = ghc::filesystem;

Resource::Resource(const std::string& name, const std::string& path, ResourceManager* manager)
    : manager_(manager) {
    meta_.name = name;
    meta_.path = path;
}

Resource::~Resource() {
    Stop();
}

bool Resource::LoadManifest() {
    fs::path manifestPath = fs::path(meta_.path) / "fxmanifest.lua";
    if (!fs::exists(manifestPath)) {
        manifestPath = fs::path(meta_.path) / "resmanifest.lua";
    }

    if (!fs::exists(manifestPath)) {
        return false;
    }

    lua_State* L = luaL_newstate();
    if (!L) return false;
    luaL_openlibs(L);

    // Context for manifest parsing
    lua_pushlightuserdata(L, this);
    lua_setglobal(L, "__res_ptr");

    // Manifest DSL helper
    const char* dsl = R"lua(
        local res = __res_ptr

        function fx_version(v) end
        function game(g) end
        function author(a) __set_author(a) end
        function version(v) __set_version(v) end
        function description(d) __set_desc(d) end

        function server_scripts(tbl)
            if type(tbl) == "table" then
                for _, s in ipairs(tbl) do
                    __add_server_script(s)
                end
            elseif type(tbl) == "string" then
                __add_server_script(tbl)
            end
        end

        function shared_scripts(tbl)
            if type(tbl) == "table" then
                for _, s in ipairs(tbl) do
                    __add_shared_script(s)
                end
            elseif type(tbl) == "string" then
                __add_shared_script(tbl)
            end
        end

        function dependencies(tbl)
            if type(tbl) == "table" then
                for _, d in ipairs(tbl) do
                    __add_dependency(d)
                end
            end
        end
    )lua";

    luaL_dostring(L, dsl);

    // C callbacks for DSL
    lua_register(L, "__set_author", [](lua_State* L) -> int {
        Resource* self = static_cast<Resource*>(lua_touserdata(L, lua_upvalueindex(1)));
        if (self && lua_isstring(L, 1)) self->meta_.author = lua_tostring(L, 1);
        return 0;
    });

    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, [](lua_State* L) -> int {
        Resource* self = static_cast<Resource*>(lua_touserdata(L, lua_upvalueindex(1)));
        if (self && lua_isstring(L, 1)) self->meta_.version = lua_tostring(L, 1);
        return 0;
    }, 1);
    lua_setglobal(L, "__set_version");

    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, [](lua_State* L) -> int {
        Resource* self = static_cast<Resource*>(lua_touserdata(L, lua_upvalueindex(1)));
        if (self && lua_isstring(L, 1)) self->meta_.description = lua_tostring(L, 1);
        return 0;
    }, 1);
    lua_setglobal(L, "__set_desc");

    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, [](lua_State* L) -> int {
        Resource* self = static_cast<Resource*>(lua_touserdata(L, lua_upvalueindex(1)));
        if (self && lua_isstring(L, 1)) self->meta_.serverScripts.push_back(lua_tostring(L, 1));
        return 0;
    }, 1);
    lua_setglobal(L, "__add_server_script");

    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, [](lua_State* L) -> int {
        Resource* self = static_cast<Resource*>(lua_touserdata(L, lua_upvalueindex(1)));
        if (self && lua_isstring(L, 1)) self->meta_.sharedScripts.push_back(lua_tostring(L, 1));
        return 0;
    }, 1);
    lua_setglobal(L, "__add_shared_script");

    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, [](lua_State* L) -> int {
        Resource* self = static_cast<Resource*>(lua_touserdata(L, lua_upvalueindex(1)));
        if (self && lua_isstring(L, 1)) self->meta_.dependencies.push_back(lua_tostring(L, 1));
        return 0;
    }, 1);
    lua_setglobal(L, "__add_dependency");

    if (luaL_dofile(L, manifestPath.string().c_str()) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        manager_->LogError(std::string("Error reading manifest for ") + meta_.name + ": " + (err ? err : ""));
        lua_close(L);
        return false;
    }

    lua_close(L);
    return true;
}

void Resource::InitLuaEnvironment() {
    L_ = luaL_newstate();
    if (!L_) return;

    luaL_openlibs(L_);
    RegisterBindings(L_, this);
}

void Resource::CleanupLuaEnvironment() {
    if (L_) {
        lua_close(L_);
        L_ = nullptr;
    }
}

bool Resource::ExecuteScript(const std::string& relativePath) {
    fs::path fullPath = fs::path(meta_.path) / relativePath;
    if (!fs::exists(fullPath)) {
        manager_->LogError("Script file not found: " + fullPath.string());
        return false;
    }

    if (luaL_dofile(L_, fullPath.string().c_str()) != LUA_OK) {
        const char* err = lua_tostring(L_, -1);
        manager_->LogError(std::string("[Lua Error in ") + meta_.name + " -> " + relativePath + "]: " + (err ? err : "unknown error"));
        lua_pop(L_, 1);
        return false;
    }

    return true;
}

bool Resource::Start() {
    if (state_ == ResourceState::Running) {
        manager_->Log("Resource " + meta_.name + " is already running.");
        return true;
    }

    state_ = ResourceState::Starting;
    manager_->Log("Starting resource: " + meta_.name);

    InitLuaEnvironment();
    if (!L_) {
        manager_->LogError("Failed to initialize Lua state for resource: " + meta_.name);
        state_ = ResourceState::Stopped;
        return false;
    }

    // Execute shared scripts first
    for (const auto& script : meta_.sharedScripts) {
        if (!ExecuteScript(script)) {
            Stop();
            return false;
        }
    }

    // Execute server scripts
    for (const auto& script : meta_.serverScripts) {
        if (!ExecuteScript(script)) {
            Stop();
            return false;
        }
    }

    state_ = ResourceState::Running;
    manager_->Log("Resource started: " + meta_.name + " (v" + meta_.version + ")");

    // Fire onResourceStart event
    std::vector<LuaValue> args = { meta_.name };
    EventManager::Get().TriggerEvent("onResourceStart", args);

    return true;
}

bool Resource::Stop() {
    if (state_ == ResourceState::Stopped) {
        return true;
    }

    state_ = ResourceState::Stopping;
    manager_->Log("Stopping resource: " + meta_.name);

    // Fire onResourceStop event
    std::vector<LuaValue> args = { meta_.name };
    EventManager::Get().TriggerEvent("onResourceStop", args);

    EventManager::Get().RemoveListenersForResource(this);
    CleanupLuaEnvironment();

    state_ = ResourceState::Stopped;
    manager_->Log("Resource stopped: " + meta_.name);
    return true;
}

bool Resource::Restart() {
    Stop();
    LoadManifest();
    return Start();
}

} // namespace Resa
