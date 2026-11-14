/*
 *  resa-multiplayer (resam)
 *  Lua Native Bindings Implementation
 */

#include "bindings.hpp"
#include "resource.hpp"
#include "resource_manager.hpp"
#include "event_manager.hpp"
#include <Server/Components/Vehicles/vehicles.hpp>
#include <Server/Components/Dialogs/dialogs.hpp>
#include <Server/Components/Pickups/pickups.hpp>
#include <Server/Components/TextLabels/textlabels.hpp>
#include <Server/Components/Objects/objects.hpp>
#include <Server/Components/Timers/timers.hpp>
#include <Server/Components/Checkpoints/checkpoints.hpp>
#include <Server/Components/Actors/actors.hpp>
#include <Server/Components/Variables/variables.hpp>
#include <Server/Components/Classes/classes.hpp>
#include <sstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>

namespace Resa {

// ============================================================================
// 1. Core Utilities & Commands
// ============================================================================

static int Lua_Print(lua_State* L) {
    int nargs = lua_gettop(L);
    std::ostringstream ss;
    for (int i = 1; i <= nargs; ++i) {
        if (i > 1) ss << "\t";
        if (lua_isstring(L, i)) {
            ss << lua_tostring(L, i);
        } else if (lua_isboolean(L, i)) {
            ss << (lua_toboolean(L, i) ? "true" : "false");
        } else if (lua_isnil(L, i)) {
            ss << "nil";
        } else {
            ss << lua_typename(L, lua_type(L, i)) << ": " << lua_topointer(L, i);
        }
    }
    ResourceManager::Get().Log(ss.str());
    return 0;
}

static int Lua_AddEventHandler(lua_State* L) {
    const char* eventName = luaL_checkstring(L, 1);
    if (!lua_isfunction(L, 2)) {
        return luaL_error(L, "AddEventHandler expected function as 2nd argument");
    }

    lua_pushvalue(L, 2);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);

    lua_pushstring(L, "__resa_resource");
    lua_gettable(L, LUA_REGISTRYINDEX);
    Resource* res = static_cast<Resource*>(lua_touserdata(L, -1));
    lua_pop(L, 1);

    if (res) {
        EventManager::Get().AddListener(eventName, res, ref);
    }
    return 0;
}

static int Lua_TriggerEvent(lua_State* L) {
    const char* eventName = luaL_checkstring(L, 1);
    int nargs = lua_gettop(L);

    std::vector<LuaValue> args;
    for (int i = 2; i <= nargs; ++i) {
        if (lua_isboolean(L, i)) {
            args.push_back(static_cast<bool>(lua_toboolean(L, i)));
        } else if (lua_isinteger(L, i)) {
            args.push_back(static_cast<int>(lua_tointeger(L, i)));
        } else if (lua_isnumber(L, i)) {
            args.push_back(lua_tonumber(L, i));
        } else if (lua_isstring(L, i)) {
            args.push_back(std::string(lua_tostring(L, i)));
        } else {
            args.push_back(std::monostate{});
        }
    }

    EventManager::Get().TriggerEvent(eventName, args);
    return 0;
}

// ----------------------------------------------------------------------------
// RegisterCommand System
// ----------------------------------------------------------------------------
struct CommandHandler {
    Resource* resource;
    int luaRef;
};
static std::unordered_map<std::string, std::vector<CommandHandler>> g_RegisteredCommands;

static int Lua_RegisterCommand(lua_State* L) {
    const char* rawCmd = luaL_checkstring(L, 1);
    if (!lua_isfunction(L, 2)) {
        return luaL_error(L, "RegisterCommand expects a function as 2nd argument");
    }

    std::string name(rawCmd);
    if (!name.empty() && name[0] == '/') {
        name = name.substr(1);
    }
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);

    lua_pushvalue(L, 2);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);

    lua_pushstring(L, "__resa_resource");
    lua_gettable(L, LUA_REGISTRYINDEX);
    Resource* res = static_cast<Resource*>(lua_touserdata(L, -1));
    lua_pop(L, 1);

    if (res) {
        g_RegisteredCommands[name].push_back({ res, ref });
    }
    return 0;
}

bool DispatchCommand(int playerId, const std::string& fullCommand) {
    if (fullCommand.empty()) return false;
    std::string text = fullCommand;
    if (text[0] == '/') text = text.substr(1);

    std::istringstream iss(text);
    std::string cmdName;
    if (!(iss >> cmdName)) return false;
    std::transform(cmdName.begin(), cmdName.end(), cmdName.begin(), ::tolower);

    std::vector<std::string> args;
    std::string arg;
    while (iss >> arg) {
        args.push_back(arg);
    }

    auto it = g_RegisteredCommands.find(cmdName);
    if (it == g_RegisteredCommands.end() || it->second.empty()) {
        return false;
    }

    bool handled = false;
    for (const auto& cmd : it->second) {
        if (!cmd.resource || cmd.resource->GetState() != ResourceState::Running) continue;
        lua_State* L = cmd.resource->GetLua();
        if (!L) continue;

        lua_rawgeti(L, LUA_REGISTRYINDEX, cmd.luaRef);
        if (!lua_isfunction(L, -1)) {
            lua_pop(L, 1);
            continue;
        }

        lua_pushinteger(L, playerId);
        lua_newtable(L);
        for (size_t i = 0; i < args.size(); ++i) {
            lua_pushstring(L, args[i].c_str());
            lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
        }

        if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
            const char* err = lua_tostring(L, -1);
            ResourceManager::Get().LogError(std::string("[Command Error][") + cmd.resource->GetName() + "] /" + cmdName + ": " + (err ? err : ""));
            lua_pop(L, 1);
        }
        handled = true;
    }
    return handled;
}

void RemoveCommandsForResource(Resource* resource) {
    for (auto& pair : g_RegisteredCommands) {
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

// ----------------------------------------------------------------------------
// Timer System (SetTimer, SetTimeout, KillTimer)
// ----------------------------------------------------------------------------
class ResaTimerHandler final : public TimerTimeOutHandler {
public:
    ResaTimerHandler(Resource* res, int ref, uint32_t id, bool repeating)
        : res_(res), ref_(ref), id_(id), repeating_(repeating) {}

    void timeout(ITimer& timer) override;
    void free(ITimer& timer) override;

    Resource* GetResource() const { return res_; }
    uint32_t GetId() const { return id_; }
    ITimer* timer_ = nullptr;

private:
    Resource* res_;
    int ref_;
    uint32_t id_;
    bool repeating_;
};

static uint32_t g_NextTimerId = 1;
static std::unordered_map<uint32_t, ResaTimerHandler*> g_ActiveTimers;

void ResaTimerHandler::timeout(ITimer& timer) {
    if (!res_ || res_->GetState() != ResourceState::Running) {
        timer.kill();
        return;
    }
    lua_State* L = res_->GetLua();
    if (!L) {
        timer.kill();
        return;
    }

    lua_rawgeti(L, LUA_REGISTRYINDEX, ref_);
    if (lua_isfunction(L, -1)) {
        if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
            const char* err = lua_tostring(L, -1);
            ResourceManager::Get().LogError(std::string("[Timer Error][") + res_->GetName() + "]: " + (err ? err : ""));
            lua_pop(L, 1);
        }
    } else {
        lua_pop(L, 1);
    }

    if (!repeating_) {
        timer.kill();
    }
}

void ResaTimerHandler::free(ITimer& timer) {
    g_ActiveTimers.erase(id_);
    if (res_ && res_->GetLua()) {
        luaL_unref(res_->GetLua(), LUA_REGISTRYINDEX, ref_);
    }
    delete this;
}

static int Lua_SetTimer(lua_State* L) {
    if (!lua_isfunction(L, 1)) {
        return luaL_error(L, "SetTimer expected function as 1st argument");
    }
    int intervalMs = static_cast<int>(luaL_checkinteger(L, 2));
    bool repeating = lua_toboolean(L, 3) != 0;

    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) {
        lua_pushnil(L);
        return 1;
    }
    ITimersComponent* tc = cl->queryComponent<ITimersComponent>();
    if (!tc) {
        lua_pushnil(L);
        return 1;
    }

    lua_pushstring(L, "__resa_resource");
    lua_gettable(L, LUA_REGISTRYINDEX);
    Resource* res = static_cast<Resource*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (!res) {
        lua_pushnil(L);
        return 1;
    }

    lua_pushvalue(L, 1);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);

    uint32_t timerId = g_NextTimerId++;
    ResaTimerHandler* handler = new ResaTimerHandler(res, ref, timerId, repeating);
    ITimer* timer = tc->create(handler, Milliseconds(intervalMs), repeating);
    if (!timer) {
        delete handler;
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        lua_pushnil(L);
        return 1;
    }
    handler->timer_ = timer;
    g_ActiveTimers[timerId] = handler;

    lua_pushinteger(L, timerId);
    return 1;
}

static int Lua_SetTimeout(lua_State* L) {
    if (!lua_isfunction(L, 1)) {
        return luaL_error(L, "SetTimeout expected function as 1st argument");
    }
    int delayMs = static_cast<int>(luaL_checkinteger(L, 2));
    lua_pushcfunction(L, Lua_SetTimer);
    lua_pushvalue(L, 1);
    lua_pushinteger(L, delayMs);
    lua_pushboolean(L, false);
    lua_call(L, 3, 1);
    return 1;
}

static int Lua_KillTimer(lua_State* L) {
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto it = g_ActiveTimers.find(id);
    if (it != g_ActiveTimers.end()) {
        if (it->second->timer_) {
            it->second->timer_->kill();
        }
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

void KillTimersForResource(Resource* resource) {
    std::vector<uint32_t> toKill;
    for (auto& pair : g_ActiveTimers) {
        if (pair.second->GetResource() == resource) {
            toKill.push_back(pair.first);
        }
    }
    for (uint32_t id : toKill) {
        auto it = g_ActiveTimers.find(id);
        if (it != g_ActiveTimers.end() && it->second->timer_) {
            it->second->timer_->kill();
        }
    }
}

// ----------------------------------------------------------------------------
// Inter-Resource Exports
// ----------------------------------------------------------------------------
static int Lua_RegisterExport(lua_State* L) {
    const char* exportName = luaL_checkstring(L, 1);
    if (!lua_isfunction(L, 2)) {
        return luaL_error(L, "RegisterExport expected function as 2nd argument");
    }
    lua_pushstring(L, "__resa_resource");
    lua_gettable(L, LUA_REGISTRYINDEX);
    Resource* res = static_cast<Resource*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (!res) return 0;

    lua_pushvalue(L, 2);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    ResourceManager::Get().RegisterExport(res->GetName(), exportName, ref);
    return 0;
}

static int Lua_CallExport(lua_State* L) {
    const char* resName = luaL_checkstring(L, 1);
    const char* funcName = luaL_checkstring(L, 2);
    int nargs = lua_gettop(L) - 2;

    Resource* targetRes = ResourceManager::Get().GetResource(resName);
    if (!targetRes || targetRes->GetState() != ResourceState::Running) {
        return luaL_error(L, "Target resource '%s' is not running", resName);
    }

    int ref = ResourceManager::Get().GetExportRef(resName, funcName);
    if (ref == LUA_NOREF) {
        return luaL_error(L, "Exported function '%s' not found in resource '%s'", funcName, resName);
    }

    lua_State* targetL = targetRes->GetLua();
    if (!targetL) {
        return luaL_error(L, "Lua state not available for resource '%s'", resName);
    }

    int targetTop = lua_gettop(targetL);
    lua_rawgeti(targetL, LUA_REGISTRYINDEX, ref);
    if (!lua_isfunction(targetL, -1)) {
        lua_settop(targetL, targetTop);
        return luaL_error(L, "Export '%s' in resource '%s' is not a function", funcName, resName);
    }

    for (int i = 3; i <= 2 + nargs; ++i) {
        if (lua_isboolean(L, i)) {
            lua_pushboolean(targetL, lua_toboolean(L, i));
        } else if (lua_isinteger(L, i)) {
            lua_pushinteger(targetL, lua_tointeger(L, i));
        } else if (lua_isnumber(L, i)) {
            lua_pushnumber(targetL, lua_tonumber(L, i));
        } else if (lua_isstring(L, i)) {
            lua_pushstring(targetL, lua_tostring(L, i));
        } else if (lua_isnil(L, i)) {
            lua_pushnil(targetL);
        } else {
            lua_pushnil(targetL);
        }
    }

    int status = lua_pcall(targetL, nargs, LUA_MULTRET, 0);
    if (status != LUA_OK) {
        const char* err = lua_tostring(targetL, -1);
        std::string errMsg = err ? err : "unknown error";
        lua_settop(targetL, targetTop);
        return luaL_error(L, "Error in export '%s' of '%s': %s", funcName, resName, errMsg.c_str());
    }

    int returnCount = lua_gettop(targetL) - targetTop;
    for (int i = 1; i <= returnCount; ++i) {
        int idx = targetTop + i;
        if (lua_isboolean(targetL, idx)) {
            lua_pushboolean(L, lua_toboolean(targetL, idx));
        } else if (lua_isinteger(targetL, idx)) {
            lua_pushinteger(L, lua_tointeger(targetL, idx));
        } else if (lua_isnumber(targetL, idx)) {
            lua_pushnumber(L, lua_tonumber(targetL, idx));
        } else if (lua_isstring(targetL, idx)) {
            lua_pushstring(L, lua_tostring(targetL, idx));
        } else if (lua_isnil(targetL, idx)) {
            lua_pushnil(L);
        } else {
            lua_pushnil(L);
        }
    }
    lua_settop(targetL, targetTop);
    return returnCount;
}

// ============================================================================
// 2. Player Native API
// ============================================================================

static int Lua_GetPlayerName(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (!player) {
        lua_pushnil(L);
        return 1;
    }
    StringView name = player->getName();
    lua_pushlstring(L, name.data(), name.length());
    return 1;
}

static int Lua_GetPlayerPos(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (!player) return 0;

    Vector3 pos = player->getPosition();
    lua_pushnumber(L, pos.x);
    lua_pushnumber(L, pos.y);
    lua_pushnumber(L, pos.z);
    return 3;
}

static int Lua_SetPlayerPos(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    float x = static_cast<float>(luaL_checknumber(L, 2));
    float y = static_cast<float>(luaL_checknumber(L, 3));
    float z = static_cast<float>(luaL_checknumber(L, 4));

    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        player->setPosition(Vector3(x, y, z));
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_GetPlayerFacingAngle(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (!player) {
        lua_pushnil(L);
        return 1;
    }
    float angle = player->getRotation().ToEuler().z;
    lua_pushnumber(L, angle);
    return 1;
}

static int Lua_SetPlayerFacingAngle(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    float angle = static_cast<float>(luaL_checknumber(L, 2));

    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        Vector3 rot = player->getRotation().ToEuler();
        rot.z = angle;
        player->setRotation(rot);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_GetPlayerHealth(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (!player) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushnumber(L, player->getHealth());
    return 1;
}

static int Lua_SetPlayerHealth(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    float health = static_cast<float>(luaL_checknumber(L, 2));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        player->setHealth(health);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_GetPlayerArmour(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (!player) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushnumber(L, player->getArmour());
    return 1;
}

static int Lua_SetPlayerArmour(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    float armour = static_cast<float>(luaL_checknumber(L, 2));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        player->setArmour(armour);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_GetPlayerSkin(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        lua_pushinteger(L, player->getSkin());
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int Lua_SetPlayerSkin(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    int skin = static_cast<int>(luaL_checkinteger(L, 2));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        player->setSkin(skin);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_SendClientMessage(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    uint32_t colour = static_cast<uint32_t>(luaL_checkinteger(L, 2));
    const char* text = luaL_checkstring(L, 3);

    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        player->sendClientMessage(Colour::FromRGBA(colour), StringView(text));
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_SendClientMessageToAll(lua_State* L) {
    uint32_t colour = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const char* text = luaL_checkstring(L, 2);

    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    core->getPlayers().sendClientMessageToAll(Colour::FromRGBA(colour), StringView(text));
    return 0;
}

static int Lua_SpawnPlayer(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        player->spawn();
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_KickPlayer(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        player->kick();
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_GivePlayerWeapon(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    int weapon = static_cast<int>(luaL_checkinteger(L, 2));
    int ammo = static_cast<int>(luaL_checkinteger(L, 3));

    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        player->giveWeapon(WeaponSlotData{ static_cast<uint8_t>(weapon), static_cast<uint32_t>(ammo) });
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_ResetPlayerWeapons(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        player->resetWeapons();
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_GetPlayerWeapon(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        lua_pushinteger(L, player->getArmedWeapon());
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int Lua_GetPlayerAmmo(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        lua_pushinteger(L, player->getArmedWeaponAmmo());
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int Lua_SetPlayerArmedWeapon(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    int weapon = static_cast<int>(luaL_checkinteger(L, 2));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        player->setArmedWeapon(weapon);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_GivePlayerMoney(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    int money = static_cast<int>(luaL_checkinteger(L, 2));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        player->giveMoney(money);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_GetPlayerMoney(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        lua_pushinteger(L, player->getMoney());
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int Lua_ResetPlayerMoney(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        player->resetMoney();
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_SetPlayerMoney(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    int money = static_cast<int>(luaL_checkinteger(L, 2));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        player->setMoney(money);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_SetPlayerInterior(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    unsigned interior = static_cast<unsigned>(luaL_checkinteger(L, 2));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        player->setInterior(interior);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_GetPlayerInterior(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        lua_pushinteger(L, player->getInterior());
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int Lua_SetPlayerVirtualWorld(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    int vw = static_cast<int>(luaL_checkinteger(L, 2));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        player->setVirtualWorld(vw);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_GetPlayerVirtualWorld(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        lua_pushinteger(L, player->getVirtualWorld());
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int Lua_SetPlayerVelocity(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    float x = static_cast<float>(luaL_checknumber(L, 2));
    float y = static_cast<float>(luaL_checknumber(L, 3));
    float z = static_cast<float>(luaL_checknumber(L, 4));

    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        player->setVelocity(Vector3(x, y, z));
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_GetPlayerVelocity(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        Vector3 v = player->getVelocity();
        lua_pushnumber(L, v.x);
        lua_pushnumber(L, v.y);
        lua_pushnumber(L, v.z);
        return 3;
    }
    return 0;
}

static int Lua_PutPlayerInVehicle(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    int vehicleId = static_cast<int>(luaL_checkinteger(L, 2));
    int seat = static_cast<int>(luaL_optinteger(L, 3, 0));

    ICore* core = ResourceManager::Get().GetCore();
    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!core || !cl) return 0;

    IPlayer* player = core->getPlayers().get(playerId);
    IVehiclesComponent* vehicles = cl->queryComponent<IVehiclesComponent>();
    if (!player || !vehicles) {
        lua_pushboolean(L, false);
        return 1;
    }

    IVehicle* veh = vehicles->get(vehicleId);
    if (veh) {
        veh->putPlayer(*player, seat);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_RemovePlayerFromVehicle(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    bool force = lua_toboolean(L, 2) != 0;

    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(playerId);
    if (player) {
        player->removeFromVehicle(force);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_GetPlayerVehicleID(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(playerId);
    if (!player) {
        lua_pushnil(L);
        return 1;
    }

    IPlayerVehicleData* pvd = queryExtension<IPlayerVehicleData>(player);
    if (pvd && pvd->getVehicle()) {
        lua_pushinteger(L, pvd->getVehicle()->getID());
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int Lua_GetPlayerVehicleSeat(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(playerId);
    if (!player) {
        lua_pushnil(L);
        return 1;
    }

    IPlayerVehicleData* pvd = queryExtension<IPlayerVehicleData>(player);
    if (pvd && pvd->getVehicle()) {
        lua_pushinteger(L, pvd->getSeat());
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int Lua_IsPlayerInAnyVehicle(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(playerId);
    if (!player) {
        lua_pushboolean(L, false);
        return 1;
    }

    IPlayerVehicleData* pvd = queryExtension<IPlayerVehicleData>(player);
    lua_pushboolean(L, pvd && pvd->getVehicle() != nullptr);
    return 1;
}

static int Lua_SetPlayerScore(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    int score = static_cast<int>(luaL_checkinteger(L, 2));

    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        player->setScore(score);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_GetPlayerScore(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        lua_pushinteger(L, player->getScore());
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int Lua_GetPlayerPing(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        lua_pushinteger(L, player->getPing());
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int Lua_ApplyAnimation(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    const char* animLib = luaL_checkstring(L, 2);
    const char* animName = luaL_checkstring(L, 3);
    float delta = static_cast<float>(luaL_optnumber(L, 4, 4.1));
    bool loop = lua_toboolean(L, 5) != 0;
    bool lockX = lua_toboolean(L, 6) != 0;
    bool lockY = lua_toboolean(L, 7) != 0;
    bool freeze = lua_toboolean(L, 8) != 0;
    uint32_t time = static_cast<uint32_t>(luaL_optinteger(L, 9, 0));

    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        AnimationData anim;
        anim.lib = animLib;
        anim.name = animName;
        anim.delta = delta;
        anim.loop = loop;
        anim.lockX = lockX;
        anim.lockY = lockY;
        anim.freeze = freeze;
        anim.time = time;
        player->applyAnimation(anim, PlayerAnimationSyncType_Sync);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_ClearAnimations(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        player->clearAnimations(PlayerAnimationSyncType_Sync);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

// ============================================================================
// Camera & Spectating (Item 1)
// ============================================================================

static int Lua_SetPlayerCameraPos(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    float x = static_cast<float>(luaL_checknumber(L, 2));
    float y = static_cast<float>(luaL_checknumber(L, 3));
    float z = static_cast<float>(luaL_checknumber(L, 4));

    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        player->setCameraPosition(Vector3(x, y, z));
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_GetPlayerCameraPos(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        Vector3 pos = player->getCameraPosition();
        lua_pushnumber(L, pos.x);
        lua_pushnumber(L, pos.y);
        lua_pushnumber(L, pos.z);
        return 3;
    }
    return 0;
}

static int Lua_SetPlayerCameraLookAt(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    float x = static_cast<float>(luaL_checknumber(L, 2));
    float y = static_cast<float>(luaL_checknumber(L, 3));
    float z = static_cast<float>(luaL_checknumber(L, 4));
    int cutType = static_cast<int>(luaL_optinteger(L, 5, 2)); // 2 = CUT

    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        player->setCameraLookAt(Vector3(x, y, z), cutType);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_GetPlayerCameraLookAt(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        Vector3 pos = player->getCameraLookAt();
        lua_pushnumber(L, pos.x);
        lua_pushnumber(L, pos.y);
        lua_pushnumber(L, pos.z);
        return 3;
    }
    return 0;
}

static int Lua_SetCameraBehindPlayer(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        player->setCameraBehind();
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_InterpolateCameraPos(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    float fx = static_cast<float>(luaL_checknumber(L, 2));
    float fy = static_cast<float>(luaL_checknumber(L, 3));
    float fz = static_cast<float>(luaL_checknumber(L, 4));
    float tx = static_cast<float>(luaL_checknumber(L, 5));
    float ty = static_cast<float>(luaL_checknumber(L, 6));
    float tz = static_cast<float>(luaL_checknumber(L, 7));
    int time = static_cast<int>(luaL_checkinteger(L, 8));
    int cutType = static_cast<int>(luaL_optinteger(L, 9, 2));

    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        player->interpolateCameraPosition(Vector3(fx, fy, fz), Vector3(tx, ty, tz), time, static_cast<PlayerCameraCutType>(cutType));
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_InterpolateCameraLookAt(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    float fx = static_cast<float>(luaL_checknumber(L, 2));
    float fy = static_cast<float>(luaL_checknumber(L, 3));
    float fz = static_cast<float>(luaL_checknumber(L, 4));
    float tx = static_cast<float>(luaL_checknumber(L, 5));
    float ty = static_cast<float>(luaL_checknumber(L, 6));
    float tz = static_cast<float>(luaL_checknumber(L, 7));
    int time = static_cast<int>(luaL_checkinteger(L, 8));
    int cutType = static_cast<int>(luaL_optinteger(L, 9, 2));

    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        player->interpolateCameraLookAt(Vector3(fx, fy, fz), Vector3(tx, ty, tz), time, static_cast<PlayerCameraCutType>(cutType));
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_TogglePlayerSpectating(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    bool toggle = lua_toboolean(L, 2) != 0;

    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        player->setSpectating(toggle);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_PlayerSpectatePlayer(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    int targetId = static_cast<int>(luaL_checkinteger(L, 2));
    int mode = static_cast<int>(luaL_optinteger(L, 3, 1)); // 1 = Normal

    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    IPlayer* target = core->getPlayers().get(targetId);
    if (player && target) {
        player->spectatePlayer(*target, static_cast<PlayerSpectateMode>(mode));
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_PlayerSpectateVehicle(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    int targetVehicleId = static_cast<int>(luaL_checkinteger(L, 2));
    int mode = static_cast<int>(luaL_optinteger(L, 3, 1));

    ICore* core = ResourceManager::Get().GetCore();
    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!core || !cl) return 0;

    IPlayer* player = core->getPlayers().get(id);
    IVehiclesComponent* vehicles = cl->queryComponent<IVehiclesComponent>();
    if (!player || !vehicles) {
        lua_pushboolean(L, false);
        return 1;
    }

    IVehicle* veh = vehicles->get(targetVehicleId);
    if (veh) {
        player->spectateVehicle(*veh, static_cast<PlayerSpectateMode>(mode));
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

// ============================================================================
// Audio & Sounds (Item 2)
// ============================================================================

static int Lua_PlayAudioStreamForPlayer(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    const char* url = luaL_checkstring(L, 2);
    float x = static_cast<float>(luaL_optnumber(L, 3, 0.0));
    float y = static_cast<float>(luaL_optnumber(L, 4, 0.0));
    float z = static_cast<float>(luaL_optnumber(L, 5, 0.0));
    float distance = static_cast<float>(luaL_optnumber(L, 6, 50.0));
    bool usePos = lua_toboolean(L, 7) != 0;

    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        player->playAudio(StringView(url), usePos, Vector3(x, y, z), distance);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_StopAudioStreamForPlayer(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        player->stopAudio();
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_PlayerPlaySound(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    uint32_t sound = static_cast<uint32_t>(luaL_checkinteger(L, 2));
    float x = static_cast<float>(luaL_optnumber(L, 3, 0.0));
    float y = static_cast<float>(luaL_optnumber(L, 4, 0.0));
    float z = static_cast<float>(luaL_optnumber(L, 5, 0.0));

    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        player->playSound(sound, Vector3(x, y, z));
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

// ============================================================================
// Character Accessories & Special Actions (Item 3)
// ============================================================================

static int Lua_SetPlayerAttachedObject(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    int slot = static_cast<int>(luaL_checkinteger(L, 2));
    int model = static_cast<int>(luaL_checkinteger(L, 3));
    int bone = static_cast<int>(luaL_checkinteger(L, 4));
    float ox = static_cast<float>(luaL_optnumber(L, 5, 0.0));
    float oy = static_cast<float>(luaL_optnumber(L, 6, 0.0));
    float oz = static_cast<float>(luaL_optnumber(L, 7, 0.0));
    float rx = static_cast<float>(luaL_optnumber(L, 8, 0.0));
    float ry = static_cast<float>(luaL_optnumber(L, 9, 0.0));
    float rz = static_cast<float>(luaL_optnumber(L, 10, 0.0));
    float sx = static_cast<float>(luaL_optnumber(L, 11, 1.0));
    float sy = static_cast<float>(luaL_optnumber(L, 12, 1.0));
    float sz = static_cast<float>(luaL_optnumber(L, 13, 1.0));
    uint32_t col1 = static_cast<uint32_t>(luaL_optinteger(L, 14, 0));
    uint32_t col2 = static_cast<uint32_t>(luaL_optinteger(L, 15, 0));

    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (!player) {
        lua_pushboolean(L, false);
        return 1;
    }

    IPlayerObjectData* data = queryExtension<IPlayerObjectData>(player);
    if (data) {
        ObjectAttachmentSlotData attachment;
        attachment.model = model;
        attachment.bone = bone;
        attachment.offset = { ox, oy, oz };
        attachment.rotation = { rx, ry, rz };
        attachment.scale = { sx, sy, sz };
        attachment.colour1 = Colour::FromARGB(col1);
        attachment.colour2 = Colour::FromARGB(col2);
        data->setAttachedObject(slot, attachment);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_RemovePlayerAttachedObject(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    int slot = static_cast<int>(luaL_checkinteger(L, 2));

    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (!player) {
        lua_pushboolean(L, false);
        return 1;
    }

    IPlayerObjectData* data = queryExtension<IPlayerObjectData>(player);
    if (data) {
        data->removeAttachedObject(slot);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_IsPlayerAttachedObjectSlotUsed(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    int slot = static_cast<int>(luaL_checkinteger(L, 2));

    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (!player) {
        lua_pushboolean(L, false);
        return 1;
    }

    IPlayerObjectData* data = queryExtension<IPlayerObjectData>(player);
    lua_pushboolean(L, data && data->hasAttachedObject(slot));
    return 1;
}

static int Lua_SetPlayerSpecialAction(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    int action = static_cast<int>(luaL_checkinteger(L, 2));

    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        player->setSpecialAction(static_cast<PlayerSpecialAction>(action));
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_GetPlayerSpecialAction(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        lua_pushinteger(L, static_cast<int>(player->getSpecialAction()));
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int Lua_SetPlayerFightingStyle(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    int style = static_cast<int>(luaL_checkinteger(L, 2));

    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        player->setFightingStyle(static_cast<PlayerFightingStyle>(style));
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_GetPlayerFightingStyle(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        lua_pushinteger(L, static_cast<int>(player->getFightingStyle()));
    } else {
        lua_pushnil(L);
    }
    return 1;
}

// ============================================================================
// Checkpoints & Map Icons (Item 4)
// ============================================================================

static int Lua_SetPlayerCheckpoint(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    float x = static_cast<float>(luaL_checknumber(L, 2));
    float y = static_cast<float>(luaL_checknumber(L, 3));
    float z = static_cast<float>(luaL_checknumber(L, 4));
    float size = static_cast<float>(luaL_optnumber(L, 5, 3.0));

    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (!player) {
        lua_pushboolean(L, false);
        return 1;
    }

    IPlayerCheckpointData* cpData = queryExtension<IPlayerCheckpointData>(player);
    if (cpData) {
        ICheckpointData& cp = cpData->getCheckpoint();
        cp.setPosition(Vector3(x, y, z));
        cp.setRadius(size);
        cp.enable();
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_DisablePlayerCheckpoint(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (!player) {
        lua_pushboolean(L, false);
        return 1;
    }

    IPlayerCheckpointData* cpData = queryExtension<IPlayerCheckpointData>(player);
    if (cpData) {
        cpData->getCheckpoint().disable();
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_SetPlayerRaceCheckpoint(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    int type = static_cast<int>(luaL_checkinteger(L, 2));
    float x = static_cast<float>(luaL_checknumber(L, 3));
    float y = static_cast<float>(luaL_checknumber(L, 4));
    float z = static_cast<float>(luaL_checknumber(L, 5));
    float nx = static_cast<float>(luaL_checknumber(L, 6));
    float ny = static_cast<float>(luaL_checknumber(L, 7));
    float nz = static_cast<float>(luaL_checknumber(L, 8));
    float size = static_cast<float>(luaL_optnumber(L, 9, 5.0));

    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (!player) {
        lua_pushboolean(L, false);
        return 1;
    }

    IPlayerCheckpointData* cpData = queryExtension<IPlayerCheckpointData>(player);
    if (cpData) {
        IRaceCheckpointData& rcp = cpData->getRaceCheckpoint();
        rcp.setType(static_cast<RaceCheckpointType>(type));
        rcp.setPosition(Vector3(x, y, z));
        rcp.setNextPosition(Vector3(nx, ny, nz));
        rcp.setRadius(size);
        rcp.enable();
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_DisablePlayerRaceCheckpoint(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (!player) {
        lua_pushboolean(L, false);
        return 1;
    }

    IPlayerCheckpointData* cpData = queryExtension<IPlayerCheckpointData>(player);
    if (cpData) {
        cpData->getRaceCheckpoint().disable();
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_SetPlayerMapIcon(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    int iconId = static_cast<int>(luaL_checkinteger(L, 2));
    float x = static_cast<float>(luaL_checknumber(L, 3));
    float y = static_cast<float>(luaL_checknumber(L, 4));
    float z = static_cast<float>(luaL_checknumber(L, 5));
    int markertype = static_cast<int>(luaL_checkinteger(L, 6));
    uint32_t colour = static_cast<uint32_t>(luaL_optinteger(L, 7, 0xFFFFFFFF));
    int style = static_cast<int>(luaL_optinteger(L, 8, 0));

    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        player->setMapIcon(iconId, Vector3(x, y, z), markertype, Colour::FromRGBA(colour), static_cast<MapIconStyle>(style));
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_RemovePlayerMapIcon(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    int iconId = static_cast<int>(luaL_checkinteger(L, 2));

    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        player->unsetMapIcon(iconId);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

// ============================================================================
// 3. Vehicle Native API & Tuning (Item 6)
// ============================================================================

static int Lua_CreateVehicle(lua_State* L) {
    int model = static_cast<int>(luaL_checkinteger(L, 1));
    float x = static_cast<float>(luaL_checknumber(L, 2));
    float y = static_cast<float>(luaL_checknumber(L, 3));
    float z = static_cast<float>(luaL_checknumber(L, 4));
    float rot = static_cast<float>(luaL_optnumber(L, 5, 0.0));
    int col1 = static_cast<int>(luaL_optinteger(L, 6, -1));
    int col2 = static_cast<int>(luaL_optinteger(L, 7, -1));

    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IVehiclesComponent* vehicles = cl->queryComponent<IVehiclesComponent>();
    if (!vehicles) return 0;

    IVehicle* veh = vehicles->create(false, model, Vector3(x, y, z), rot, col1, col2);
    if (veh) {
        lua_pushinteger(L, veh->getID());
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int Lua_DestroyVehicle(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IVehiclesComponent* vehicles = cl->queryComponent<IVehiclesComponent>();
    if (!vehicles) return 0;

    IVehicle* veh = vehicles->get(id);
    if (veh) {
        vehicles->release(id);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_SetVehiclePos(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    float x = static_cast<float>(luaL_checknumber(L, 2));
    float y = static_cast<float>(luaL_checknumber(L, 3));
    float z = static_cast<float>(luaL_checknumber(L, 4));

    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IVehiclesComponent* vehicles = cl->queryComponent<IVehiclesComponent>();
    if (!vehicles) return 0;

    IVehicle* veh = vehicles->get(id);
    if (veh) {
        veh->setPosition(Vector3(x, y, z));
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_GetVehiclePos(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IVehiclesComponent* vehicles = cl->queryComponent<IVehiclesComponent>();
    if (!vehicles) return 0;

    IVehicle* veh = vehicles->get(id);
    if (veh) {
        Vector3 pos = veh->getPosition();
        lua_pushnumber(L, pos.x);
        lua_pushnumber(L, pos.y);
        lua_pushnumber(L, pos.z);
        return 3;
    }
    return 0;
}

static int Lua_SetVehicleZAngle(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    float angle = static_cast<float>(luaL_checknumber(L, 2));

    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IVehiclesComponent* vehicles = cl->queryComponent<IVehiclesComponent>();
    if (!vehicles) return 0;

    IVehicle* veh = vehicles->get(id);
    if (veh) {
        Vector3 rot = veh->getRotation().ToEuler();
        rot.z = angle;
        veh->setRotation(rot);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_GetVehicleZAngle(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IVehiclesComponent* vehicles = cl->queryComponent<IVehiclesComponent>();
    if (!vehicles) return 0;

    IVehicle* veh = vehicles->get(id);
    if (veh) {
        lua_pushnumber(L, veh->getRotation().ToEuler().z);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int Lua_SetVehicleHealth(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    float health = static_cast<float>(luaL_checknumber(L, 2));

    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IVehiclesComponent* vehicles = cl->queryComponent<IVehiclesComponent>();
    if (!vehicles) return 0;

    IVehicle* veh = vehicles->get(id);
    if (veh) {
        veh->setHealth(health);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_GetVehicleHealth(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IVehiclesComponent* vehicles = cl->queryComponent<IVehiclesComponent>();
    if (!vehicles) return 0;

    IVehicle* veh = vehicles->get(id);
    if (veh) {
        lua_pushnumber(L, veh->getHealth());
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int Lua_RepairVehicle(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IVehiclesComponent* vehicles = cl->queryComponent<IVehiclesComponent>();
    if (!vehicles) return 0;

    IVehicle* veh = vehicles->get(id);
    if (veh) {
        veh->repair();
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_SetVehicleVelocity(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    float x = static_cast<float>(luaL_checknumber(L, 2));
    float y = static_cast<float>(luaL_checknumber(L, 3));
    float z = static_cast<float>(luaL_checknumber(L, 4));

    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IVehiclesComponent* vehicles = cl->queryComponent<IVehiclesComponent>();
    if (!vehicles) return 0;

    IVehicle* veh = vehicles->get(id);
    if (veh) {
        veh->setVelocity(Vector3(x, y, z));
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_GetVehicleVelocity(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IVehiclesComponent* vehicles = cl->queryComponent<IVehiclesComponent>();
    if (!vehicles) return 0;

    IVehicle* veh = vehicles->get(id);
    if (veh) {
        Vector3 v = veh->getVelocity();
        lua_pushnumber(L, v.x);
        lua_pushnumber(L, v.y);
        lua_pushnumber(L, v.z);
        return 3;
    }
    return 0;
}

static int Lua_SetVehicleParamsEx(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    int engine = static_cast<int>(luaL_optinteger(L, 2, -1));
    int lights = static_cast<int>(luaL_optinteger(L, 3, -1));
    int alarm = static_cast<int>(luaL_optinteger(L, 4, -1));
    int doors = static_cast<int>(luaL_optinteger(L, 5, -1));
    int bonnet = static_cast<int>(luaL_optinteger(L, 6, -1));
    int boot = static_cast<int>(luaL_optinteger(L, 7, -1));
    int objective = static_cast<int>(luaL_optinteger(L, 8, -1));

    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IVehiclesComponent* vehicles = cl->queryComponent<IVehiclesComponent>();
    if (!vehicles) return 0;

    IVehicle* veh = vehicles->get(id);
    if (veh) {
        VehicleParams params = veh->getParams();
        if (engine != -1) params.engine = engine;
        if (lights != -1) params.lights = lights;
        if (alarm != -1) params.alarm = alarm;
        if (doors != -1) params.doors = doors;
        if (bonnet != -1) params.bonnet = bonnet;
        if (boot != -1) params.boot = boot;
        if (objective != -1) params.objective = objective;
        veh->setParams(params);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_GetVehicleParamsEx(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IVehiclesComponent* vehicles = cl->queryComponent<IVehiclesComponent>();
    if (!vehicles) return 0;

    IVehicle* veh = vehicles->get(id);
    if (!veh) {
        lua_pushnil(L);
        return 1;
    }

    const VehicleParams& p = veh->getParams();
    lua_newtable(L);
    lua_pushinteger(L, p.engine); lua_setfield(L, -2, "engine");
    lua_pushinteger(L, p.lights); lua_setfield(L, -2, "lights");
    lua_pushinteger(L, p.alarm); lua_setfield(L, -2, "alarm");
    lua_pushinteger(L, p.doors); lua_setfield(L, -2, "doors");
    lua_pushinteger(L, p.bonnet); lua_setfield(L, -2, "bonnet");
    lua_pushinteger(L, p.boot); lua_setfield(L, -2, "boot");
    lua_pushinteger(L, p.objective); lua_setfield(L, -2, "objective");
    return 1;
}

static int Lua_ChangeVehicleColor(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    int col1 = static_cast<int>(luaL_checkinteger(L, 2));
    int col2 = static_cast<int>(luaL_checkinteger(L, 3));

    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IVehiclesComponent* vehicles = cl->queryComponent<IVehiclesComponent>();
    if (!vehicles) return 0;

    IVehicle* veh = vehicles->get(id);
    if (veh) {
        veh->setColour(col1, col2);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_ChangeVehiclePaintjob(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    int paintjob = static_cast<int>(luaL_checkinteger(L, 2));

    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IVehiclesComponent* vehicles = cl->queryComponent<IVehiclesComponent>();
    if (!vehicles) return 0;

    IVehicle* veh = vehicles->get(id);
    if (veh) {
        veh->setPaintJob(paintjob);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_SetVehicleVirtualWorld(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    int vw = static_cast<int>(luaL_checkinteger(L, 2));

    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IVehiclesComponent* vehicles = cl->queryComponent<IVehiclesComponent>();
    if (!vehicles) return 0;

    IVehicle* veh = vehicles->get(id);
    if (veh) {
        veh->setVirtualWorld(vw);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_GetVehicleVirtualWorld(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IVehiclesComponent* vehicles = cl->queryComponent<IVehiclesComponent>();
    if (!vehicles) return 0;

    IVehicle* veh = vehicles->get(id);
    if (veh) {
        lua_pushinteger(L, veh->getVirtualWorld());
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int Lua_AddVehicleComponent(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    int comp = static_cast<int>(luaL_checkinteger(L, 2));

    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IVehiclesComponent* vehicles = cl->queryComponent<IVehiclesComponent>();
    if (!vehicles) return 0;

    IVehicle* veh = vehicles->get(id);
    if (veh) {
        veh->addComponent(comp);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_RemoveVehicleComponent(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    int comp = static_cast<int>(luaL_checkinteger(L, 2));

    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IVehiclesComponent* vehicles = cl->queryComponent<IVehiclesComponent>();
    if (!vehicles) return 0;

    IVehicle* veh = vehicles->get(id);
    if (veh) {
        veh->removeComponent(comp);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_GetVehicleComponentInSlot(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    int slot = static_cast<int>(luaL_checkinteger(L, 2));

    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IVehiclesComponent* vehicles = cl->queryComponent<IVehiclesComponent>();
    if (!vehicles) return 0;

    IVehicle* veh = vehicles->get(id);
    if (veh) {
        lua_pushinteger(L, veh->getComponentInSlot(slot));
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int Lua_SetVehicleNumberPlate(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    const char* plate = luaL_checkstring(L, 2);

    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IVehiclesComponent* vehicles = cl->queryComponent<IVehiclesComponent>();
    if (!vehicles) return 0;

    IVehicle* veh = vehicles->get(id);
    if (veh) {
        veh->setPlate(StringView(plate));
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_SetVehicleToRespawn(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IVehiclesComponent* vehicles = cl->queryComponent<IVehiclesComponent>();
    if (!vehicles) return 0;

    IVehicle* veh = vehicles->get(id);
    if (veh) {
        veh->respawn();
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_AttachTrailerToVehicle(lua_State* L) {
    int trailerId = static_cast<int>(luaL_checkinteger(L, 1));
    int vehicleId = static_cast<int>(luaL_checkinteger(L, 2));

    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IVehiclesComponent* vehicles = cl->queryComponent<IVehiclesComponent>();
    if (!vehicles) return 0;

    IVehicle* trailer = vehicles->get(trailerId);
    IVehicle* veh = vehicles->get(vehicleId);
    if (trailer && veh) {
        veh->attachTrailer(*trailer);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_DetachTrailerFromVehicle(lua_State* L) {
    int vehicleId = static_cast<int>(luaL_checkinteger(L, 1));
    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IVehiclesComponent* vehicles = cl->queryComponent<IVehiclesComponent>();
    if (!vehicles) return 0;

    IVehicle* veh = vehicles->get(vehicleId);
    if (veh) {
        veh->detachTrailer();
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_IsTrailerAttachedToVehicle(lua_State* L) {
    int vehicleId = static_cast<int>(luaL_checkinteger(L, 1));
    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IVehiclesComponent* vehicles = cl->queryComponent<IVehiclesComponent>();
    if (!vehicles) return 0;

    IVehicle* veh = vehicles->get(vehicleId);
    lua_pushboolean(L, veh && veh->getTrailer() != nullptr);
    return 1;
}

static int Lua_GetVehicleTrailer(lua_State* L) {
    int vehicleId = static_cast<int>(luaL_checkinteger(L, 1));
    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IVehiclesComponent* vehicles = cl->queryComponent<IVehiclesComponent>();
    if (!vehicles) return 0;

    IVehicle* veh = vehicles->get(vehicleId);
    if (veh && veh->getTrailer()) {
        lua_pushinteger(L, veh->getTrailer()->getID());
    } else {
        lua_pushnil(L);
    }
    return 1;
}

// ============================================================================
// 4. Dialog API
// ============================================================================

static int Lua_ShowPlayerDialog(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    int dialogId = static_cast<int>(luaL_optinteger(L, 2, 0));
    int style = static_cast<int>(luaL_optinteger(L, 3, 0));
    const char* title = luaL_optstring(L, 4, "");
    const char* body = luaL_optstring(L, 5, "");
    const char* button1 = luaL_optstring(L, 6, "OK");
    const char* button2 = luaL_optstring(L, 7, "");

    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (!player) {
        lua_pushboolean(L, false);
        return 1;
    }

    IPlayerDialogData* data = queryExtension<IPlayerDialogData>(player);
    if (!data) {
        lua_pushboolean(L, false);
        return 1;
    }

    if (dialogId == INVALID_DIALOG_ID) {
        data->hide(*player);
    } else {
        data->show(*player, dialogId & 0xFFFF, static_cast<DialogStyle>(style), title, body, button1, button2);
    }
    lua_pushboolean(L, true);
    return 1;
}

static int Lua_HidePlayerDialog(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (!player) {
        lua_pushboolean(L, false);
        return 1;
    }

    IPlayerDialogData* data = queryExtension<IPlayerDialogData>(player);
    if (!data) {
        lua_pushboolean(L, false);
        return 1;
    }

    data->hide(*player);
    lua_pushboolean(L, true);
    return 1;
}

static int Lua_GetPlayerDialog(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (!player) {
        lua_pushnil(L);
        return 1;
    }

    IPlayerDialogData* data = queryExtension<IPlayerDialogData>(player);
    if (!data) {
        lua_pushnil(L);
        return 1;
    }

    lua_pushinteger(L, data->getActiveID());
    return 1;
}

static int Lua_GetPlayerDialogData(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (!player) {
        lua_pushnil(L);
        return 1;
    }

    IPlayerDialogData* data = queryExtension<IPlayerDialogData>(player);
    if (!data) {
        lua_pushnil(L);
        return 1;
    }

    int dialogId = INVALID_DIALOG_ID;
    DialogStyle style{};
    StringView title{};
    StringView body{};
    StringView button1{};
    StringView button2{};
    data->get(dialogId, style, title, body, button1, button2);

    lua_newtable(L);
    lua_pushinteger(L, dialogId);
    lua_setfield(L, -2, "dialogId");
    lua_pushinteger(L, static_cast<int>(style));
    lua_setfield(L, -2, "style");
    lua_pushlstring(L, title.data(), title.length());
    lua_setfield(L, -2, "title");
    lua_pushlstring(L, body.data(), body.length());
    lua_setfield(L, -2, "body");
    lua_pushlstring(L, button1.data(), button1.length());
    lua_setfield(L, -2, "button1");
    lua_pushlstring(L, button2.data(), button2.length());
    lua_setfield(L, -2, "button2");
    return 1;
}

// ============================================================================
// 5. Additional Entity Components (Pickups, TextLabels, Objects)
// ============================================================================

// ----------------------------------------------------------------------------
// Pickups
// ----------------------------------------------------------------------------
static int Lua_CreatePickup(lua_State* L) {
    int model = static_cast<int>(luaL_checkinteger(L, 1));
    int type = static_cast<int>(luaL_checkinteger(L, 2));
    float x = static_cast<float>(luaL_checknumber(L, 3));
    float y = static_cast<float>(luaL_checknumber(L, 4));
    float z = static_cast<float>(luaL_checknumber(L, 5));
    uint32_t vw = static_cast<uint32_t>(luaL_optinteger(L, 6, 0));

    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IPickupsComponent* pc = cl->queryComponent<IPickupsComponent>();
    if (!pc) return 0;

    IPickup* pickup = pc->create(model, static_cast<PickupType>(type), Vector3(x, y, z), vw, false);
    if (pickup) {
        lua_pushinteger(L, pickup->getID());
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int Lua_DestroyPickup(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IPickupsComponent* pc = cl->queryComponent<IPickupsComponent>();
    if (!pc) return 0;

    IPickup* pickup = pc->get(id);
    if (pickup) {
        pc->release(id);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

// ----------------------------------------------------------------------------
// 3D Text Labels
// ----------------------------------------------------------------------------
static int Lua_Create3DTextLabel(lua_State* L) {
    const char* text = luaL_checkstring(L, 1);
    uint32_t colour = static_cast<uint32_t>(luaL_checkinteger(L, 2));
    float x = static_cast<float>(luaL_checknumber(L, 3));
    float y = static_cast<float>(luaL_checknumber(L, 4));
    float z = static_cast<float>(luaL_checknumber(L, 5));
    float drawDist = static_cast<float>(luaL_optnumber(L, 6, 40.0));
    int vw = static_cast<int>(luaL_optinteger(L, 7, 0));
    bool testLos = lua_toboolean(L, 8) != 0;

    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    ITextLabelsComponent* tlc = cl->queryComponent<ITextLabelsComponent>();
    if (!tlc) return 0;

    ITextLabel* label = tlc->create(StringView(text), Colour::FromRGBA(colour), Vector3(x, y, z), drawDist, vw, testLos);
    if (label) {
        lua_pushinteger(L, label->getID());
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int Lua_Delete3DTextLabel(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    ITextLabelsComponent* tlc = cl->queryComponent<ITextLabelsComponent>();
    if (!tlc) return 0;

    ITextLabel* label = tlc->get(id);
    if (label) {
        tlc->release(id);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_Update3DTextLabelText(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    uint32_t colour = static_cast<uint32_t>(luaL_checkinteger(L, 2));
    const char* text = luaL_checkstring(L, 3);

    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    ITextLabelsComponent* tlc = cl->queryComponent<ITextLabelsComponent>();
    if (!tlc) return 0;

    ITextLabel* label = tlc->get(id);
    if (label) {
        label->setColourAndText(Colour::FromRGBA(colour), StringView(text));
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

// ----------------------------------------------------------------------------
// Objects
// ----------------------------------------------------------------------------
static int Lua_CreateObject(lua_State* L) {
    int model = static_cast<int>(luaL_checkinteger(L, 1));
    float x = static_cast<float>(luaL_checknumber(L, 2));
    float y = static_cast<float>(luaL_checknumber(L, 3));
    float z = static_cast<float>(luaL_checknumber(L, 4));
    float rx = static_cast<float>(luaL_optnumber(L, 5, 0.0));
    float ry = static_cast<float>(luaL_optnumber(L, 6, 0.0));
    float rz = static_cast<float>(luaL_optnumber(L, 7, 0.0));
    float drawDist = static_cast<float>(luaL_optnumber(L, 8, 300.0));

    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IObjectsComponent* oc = cl->queryComponent<IObjectsComponent>();
    if (!oc) return 0;

    IObject* obj = oc->create(model, Vector3(x, y, z), Vector3(rx, ry, rz), drawDist);
    if (obj) {
        lua_pushinteger(L, obj->getID());
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int Lua_DestroyObject(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IObjectsComponent* oc = cl->queryComponent<IObjectsComponent>();
    if (!oc) return 0;

    IObject* obj = oc->get(id);
    if (obj) {
        oc->release(id);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_SetObjectPos(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    float x = static_cast<float>(luaL_checknumber(L, 2));
    float y = static_cast<float>(luaL_checknumber(L, 3));
    float z = static_cast<float>(luaL_checknumber(L, 4));

    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IObjectsComponent* oc = cl->queryComponent<IObjectsComponent>();
    if (!oc) return 0;

    IObject* obj = oc->get(id);
    if (obj) {
        obj->setPosition(Vector3(x, y, z));
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_GetObjectPos(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IObjectsComponent* oc = cl->queryComponent<IObjectsComponent>();
    if (!oc) return 0;

    IObject* obj = oc->get(id);
    if (obj) {
        Vector3 pos = obj->getPosition();
        lua_pushnumber(L, pos.x);
        lua_pushnumber(L, pos.y);
        lua_pushnumber(L, pos.z);
        return 3;
    }
    return 0;
}

static int Lua_SetObjectRot(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    float rx = static_cast<float>(luaL_checknumber(L, 2));
    float ry = static_cast<float>(luaL_checknumber(L, 3));
    float rz = static_cast<float>(luaL_checknumber(L, 4));

    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IObjectsComponent* oc = cl->queryComponent<IObjectsComponent>();
    if (!oc) return 0;

    IObject* obj = oc->get(id);
    if (obj) {
        obj->setRotation(Vector3(rx, ry, rz));
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_GetObjectRot(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IObjectsComponent* oc = cl->queryComponent<IObjectsComponent>();
    if (!oc) return 0;

    IObject* obj = oc->get(id);
    if (obj) {
        Vector3 rot = obj->getRotation().ToEuler();
        lua_pushnumber(L, rot.x);
        lua_pushnumber(L, rot.y);
        lua_pushnumber(L, rot.z);
        return 3;
    }
    return 0;
}

// ============================================================================
// World & Environment (Item 7)
// ============================================================================

static int Lua_SetWorldTime(lua_State* L) {
    int hour = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (core) {
        core->setWorldTime(Hours(hour));
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_SetPlayerTime(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    int hour = static_cast<int>(luaL_checkinteger(L, 2));
    int minute = static_cast<int>(luaL_optinteger(L, 3, 0));

    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        player->setTime(Hours(hour), Minutes(minute));
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_GetPlayerTime(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        Pair<Hours, Minutes> t = player->getTime();
        lua_pushinteger(L, static_cast<int>(t.first.count()));
        lua_pushinteger(L, static_cast<int>(t.second.count()));
        return 2;
    }
    return 0;
}

static int Lua_SetWeather(lua_State* L) {
    int weather = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (core) {
        core->setWeather(weather);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_SetPlayerWeather(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    int weather = static_cast<int>(luaL_checkinteger(L, 2));

    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        player->setWeather(weather);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_GetPlayerWeather(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        lua_pushinteger(L, player->getWeather());
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int Lua_SetGravity(lua_State* L) {
    float gravity = static_cast<float>(luaL_checknumber(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (core) {
        core->setGravity(gravity);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_GetGravity(lua_State* L) {
    ICore* core = ResourceManager::Get().GetCore();
    if (core) {
        lua_pushnumber(L, core->getGravity());
        return 1;
    }
    return 0;
}

static int Lua_CreateExplosion(lua_State* L) {
    float x = static_cast<float>(luaL_checknumber(L, 1));
    float y = static_cast<float>(luaL_checknumber(L, 2));
    float z = static_cast<float>(luaL_checknumber(L, 3));
    int type = static_cast<int>(luaL_checkinteger(L, 4));
    float radius = static_cast<float>(luaL_checknumber(L, 5));

    ICore* core = ResourceManager::Get().GetCore();
    if (core) {
        core->getPlayers().createExplosionForAll(Vector3(x, y, z), type, radius);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_CreateExplosionForPlayer(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    float x = static_cast<float>(luaL_checknumber(L, 2));
    float y = static_cast<float>(luaL_checknumber(L, 3));
    float z = static_cast<float>(luaL_checknumber(L, 4));
    int type = static_cast<int>(luaL_checkinteger(L, 5));
    float radius = static_cast<float>(luaL_checknumber(L, 6));

    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;

    IPlayer* player = core->getPlayers().get(id);
    if (player) {
        player->createExplosion(Vector3(x, y, z), type, radius);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_UsePlayerPedAnims(lua_State* L) {
    ICore* core = ResourceManager::Get().GetCore();
    if (core) {
        *core->getConfig().getBool("game.use_player_ped_anims") = true;
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

// ============================================================================
// Static NPC / Actors (Item 9)
// ============================================================================

static int Lua_CreateActor(lua_State* L) {
    int skin = static_cast<int>(luaL_checkinteger(L, 1));
    float x = static_cast<float>(luaL_checknumber(L, 2));
    float y = static_cast<float>(luaL_checknumber(L, 3));
    float z = static_cast<float>(luaL_checknumber(L, 4));
    float angle = static_cast<float>(luaL_optnumber(L, 5, 0.0));

    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IActorsComponent* actors = cl->queryComponent<IActorsComponent>();
    if (!actors) return 0;

    IActor* actor = actors->create(skin, Vector3(x, y, z), angle);
    if (actor) {
        lua_pushinteger(L, actor->getID());
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int Lua_DestroyActor(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IActorsComponent* actors = cl->queryComponent<IActorsComponent>();
    if (!actors) return 0;

    IActor* actor = actors->get(id);
    if (actor) {
        actors->release(id);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_SetActorPos(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    float x = static_cast<float>(luaL_checknumber(L, 2));
    float y = static_cast<float>(luaL_checknumber(L, 3));
    float z = static_cast<float>(luaL_checknumber(L, 4));

    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IActorsComponent* actors = cl->queryComponent<IActorsComponent>();
    if (!actors) return 0;

    IActor* actor = actors->get(id);
    if (actor) {
        actor->setPosition(Vector3(x, y, z));
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_GetActorPos(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IActorsComponent* actors = cl->queryComponent<IActorsComponent>();
    if (!actors) return 0;

    IActor* actor = actors->get(id);
    if (actor) {
        Vector3 pos = actor->getPosition();
        lua_pushnumber(L, pos.x);
        lua_pushnumber(L, pos.y);
        lua_pushnumber(L, pos.z);
        return 3;
    }
    return 0;
}

static int Lua_SetActorFacingAngle(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    float angle = static_cast<float>(luaL_checknumber(L, 2));

    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IActorsComponent* actors = cl->queryComponent<IActorsComponent>();
    if (!actors) return 0;

    IActor* actor = actors->get(id);
    if (actor) {
        Vector3 rot = actor->getRotation().ToEuler();
        rot.z = angle;
        actor->setRotation(rot);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_GetActorFacingAngle(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IActorsComponent* actors = cl->queryComponent<IActorsComponent>();
    if (!actors) return 0;

    IActor* actor = actors->get(id);
    if (actor) {
        lua_pushnumber(L, actor->getRotation().ToEuler().z);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int Lua_SetActorHealth(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    float health = static_cast<float>(luaL_checknumber(L, 2));

    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IActorsComponent* actors = cl->queryComponent<IActorsComponent>();
    if (!actors) return 0;

    IActor* actor = actors->get(id);
    if (actor) {
        actor->setHealth(health);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_GetActorHealth(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IActorsComponent* actors = cl->queryComponent<IActorsComponent>();
    if (!actors) return 0;

    IActor* actor = actors->get(id);
    if (actor) {
        lua_pushnumber(L, actor->getHealth());
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int Lua_SetActorInvulnerable(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    bool invuln = lua_toboolean(L, 2) != 0;

    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IActorsComponent* actors = cl->queryComponent<IActorsComponent>();
    if (!actors) return 0;

    IActor* actor = actors->get(id);
    if (actor) {
        actor->setInvulnerable(invuln);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_IsActorInvulnerable(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IActorsComponent* actors = cl->queryComponent<IActorsComponent>();
    if (!actors) return 0;

    IActor* actor = actors->get(id);
    if (actor) {
        lua_pushboolean(L, actor->isInvulnerable());
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_ApplyActorAnimation(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    const char* animLib = luaL_checkstring(L, 2);
    const char* animName = luaL_checkstring(L, 3);
    float delta = static_cast<float>(luaL_optnumber(L, 4, 4.1));
    bool loop = lua_toboolean(L, 5) != 0;
    bool lockX = lua_toboolean(L, 6) != 0;
    bool lockY = lua_toboolean(L, 7) != 0;
    bool freeze = lua_toboolean(L, 8) != 0;
    uint32_t time = static_cast<uint32_t>(luaL_optinteger(L, 9, 0));

    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IActorsComponent* actors = cl->queryComponent<IActorsComponent>();
    if (!actors) return 0;

    IActor* actor = actors->get(id);
    if (actor) {
        AnimationData anim;
        anim.lib = animLib;
        anim.name = animName;
        anim.delta = delta;
        anim.loop = loop;
        anim.lockX = lockX;
        anim.lockY = lockY;
        anim.freeze = freeze;
        anim.time = time;
        actor->applyAnimation(anim);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_ClearActorAnimations(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    IComponentList* cl = ResourceManager::Get().GetComponents();
    if (!cl) return 0;

    IActorsComponent* actors = cl->queryComponent<IActorsComponent>();
    if (!actors) return 0;

    IActor* actor = actors->get(id);
    if (actor) {
        actor->clearAnimations();
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

// ============================================================================
// 11. Variables (PVar & SVar)
// ============================================================================

static int Lua_SetPVarInt(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    const char* key = luaL_checkstring(L, 2);
    int val = static_cast<int>(luaL_checkinteger(L, 3));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;
    IPlayer* player = core->getPlayers().get(playerId);
    if (!player) { lua_pushboolean(L, false); return 1; }
    IPlayerVariableData* pvar = queryExtension<IPlayerVariableData>(player);
    if (pvar) {
        pvar->setInt(key, val);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_GetPVarInt(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    const char* key = luaL_checkstring(L, 2);
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;
    IPlayer* player = core->getPlayers().get(playerId);
    if (!player) { lua_pushinteger(L, 0); return 1; }
    IPlayerVariableData* pvar = queryExtension<IPlayerVariableData>(player);
    lua_pushinteger(L, pvar ? pvar->getInt(key) : 0);
    return 1;
}

static int Lua_SetPVarFloat(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    const char* key = luaL_checkstring(L, 2);
    float val = static_cast<float>(luaL_checknumber(L, 3));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;
    IPlayer* player = core->getPlayers().get(playerId);
    if (!player) { lua_pushboolean(L, false); return 1; }
    IPlayerVariableData* pvar = queryExtension<IPlayerVariableData>(player);
    if (pvar) {
        pvar->setFloat(key, val);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_GetPVarFloat(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    const char* key = luaL_checkstring(L, 2);
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;
    IPlayer* player = core->getPlayers().get(playerId);
    if (!player) { lua_pushnumber(L, 0.0); return 1; }
    IPlayerVariableData* pvar = queryExtension<IPlayerVariableData>(player);
    lua_pushnumber(L, pvar ? static_cast<double>(pvar->getFloat(key)) : 0.0);
    return 1;
}

static int Lua_SetPVarString(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    const char* key = luaL_checkstring(L, 2);
    const char* val = luaL_checkstring(L, 3);
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;
    IPlayer* player = core->getPlayers().get(playerId);
    if (!player) { lua_pushboolean(L, false); return 1; }
    IPlayerVariableData* pvar = queryExtension<IPlayerVariableData>(player);
    if (pvar) {
        pvar->setString(key, val);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_GetPVarString(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    const char* key = luaL_checkstring(L, 2);
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;
    IPlayer* player = core->getPlayers().get(playerId);
    if (!player) { lua_pushstring(L, ""); return 1; }
    IPlayerVariableData* pvar = queryExtension<IPlayerVariableData>(player);
    if (pvar) {
        StringView sv = pvar->getString(key);
        lua_pushlstring(L, sv.data(), sv.length());
    } else {
        lua_pushstring(L, "");
    }
    return 1;
}

static int Lua_DeletePVar(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    const char* key = luaL_checkstring(L, 2);
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;
    IPlayer* player = core->getPlayers().get(playerId);
    if (!player) { lua_pushboolean(L, false); return 1; }
    IPlayerVariableData* pvar = queryExtension<IPlayerVariableData>(player);
    lua_pushboolean(L, pvar ? pvar->erase(key) : false);
    return 1;
}

static int Lua_GetPVarType(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    const char* key = luaL_checkstring(L, 2);
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;
    IPlayer* player = core->getPlayers().get(playerId);
    if (!player) { lua_pushinteger(L, VariableType_None); return 1; }
    IPlayerVariableData* pvar = queryExtension<IPlayerVariableData>(player);
    lua_pushinteger(L, pvar ? static_cast<int>(pvar->getType(key)) : VariableType_None);
    return 1;
}

static int Lua_GetPVarNameAtIndex(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    int index = static_cast<int>(luaL_checkinteger(L, 2));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;
    IPlayer* player = core->getPlayers().get(playerId);
    if (!player) { lua_pushnil(L); return 1; }
    IPlayerVariableData* pvar = queryExtension<IPlayerVariableData>(player);
    StringView key;
    if (pvar && pvar->getKeyAtIndex(index, key)) {
        lua_pushlstring(L, key.data(), key.length());
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int Lua_GetPVarsUpperIndex(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;
    IPlayer* player = core->getPlayers().get(playerId);
    if (!player) { lua_pushinteger(L, 0); return 1; }
    IPlayerVariableData* pvar = queryExtension<IPlayerVariableData>(player);
    lua_pushinteger(L, pvar ? pvar->size() : 0);
    return 1;
}

static IVariablesComponent* GetVariablesComponent() {
    IComponentList* components = ResourceManager::Get().GetComponents();
    return components ? components->queryComponent<IVariablesComponent>() : nullptr;
}

static int Lua_SetSVarInt(lua_State* L) {
    const char* key = luaL_checkstring(L, 1);
    int val = static_cast<int>(luaL_checkinteger(L, 2));
    IVariablesComponent* vars = GetVariablesComponent();
    if (vars) { vars->setInt(key, val); lua_pushboolean(L, true); }
    else { lua_pushboolean(L, false); }
    return 1;
}

static int Lua_GetSVarInt(lua_State* L) {
    const char* key = luaL_checkstring(L, 1);
    IVariablesComponent* vars = GetVariablesComponent();
    lua_pushinteger(L, vars ? vars->getInt(key) : 0);
    return 1;
}

static int Lua_SetSVarFloat(lua_State* L) {
    const char* key = luaL_checkstring(L, 1);
    float val = static_cast<float>(luaL_checknumber(L, 2));
    IVariablesComponent* vars = GetVariablesComponent();
    if (vars) { vars->setFloat(key, val); lua_pushboolean(L, true); }
    else { lua_pushboolean(L, false); }
    return 1;
}

static int Lua_GetSVarFloat(lua_State* L) {
    const char* key = luaL_checkstring(L, 1);
    IVariablesComponent* vars = GetVariablesComponent();
    lua_pushnumber(L, vars ? static_cast<double>(vars->getFloat(key)) : 0.0);
    return 1;
}

static int Lua_SetSVarString(lua_State* L) {
    const char* key = luaL_checkstring(L, 1);
    const char* val = luaL_checkstring(L, 2);
    IVariablesComponent* vars = GetVariablesComponent();
    if (vars) { vars->setString(key, val); lua_pushboolean(L, true); }
    else { lua_pushboolean(L, false); }
    return 1;
}

static int Lua_GetSVarString(lua_State* L) {
    const char* key = luaL_checkstring(L, 1);
    IVariablesComponent* vars = GetVariablesComponent();
    if (vars) {
        StringView sv = vars->getString(key);
        lua_pushlstring(L, sv.data(), sv.length());
    } else {
        lua_pushstring(L, "");
    }
    return 1;
}

static int Lua_DeleteSVar(lua_State* L) {
    const char* key = luaL_checkstring(L, 1);
    IVariablesComponent* vars = GetVariablesComponent();
    lua_pushboolean(L, vars ? vars->erase(key) : false);
    return 1;
}

static int Lua_GetSVarType(lua_State* L) {
    const char* key = luaL_checkstring(L, 1);
    IVariablesComponent* vars = GetVariablesComponent();
    lua_pushinteger(L, vars ? static_cast<int>(vars->getType(key)) : VariableType_None);
    return 1;
}

static int Lua_GetSVarNameAtIndex(lua_State* L) {
    int index = static_cast<int>(luaL_checkinteger(L, 1));
    IVariablesComponent* vars = GetVariablesComponent();
    StringView key;
    if (vars && vars->getKeyAtIndex(index, key)) {
        lua_pushlstring(L, key.data(), key.length());
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int Lua_GetSVarsUpperIndex(lua_State* L) {
    IVariablesComponent* vars = GetVariablesComponent();
    lua_pushinteger(L, vars ? vars->size() : 0);
    return 1;
}

// ============================================================================
// 12. Classes & Spawn Selection
// ============================================================================

static WeaponSlots BuildWeaponSlotsFromArgs(lua_State* L, int startIdx) {
    WeaponSlots weapons;
    for (int i = 0; i < 3; ++i) {
        uint8_t w = static_cast<uint8_t>(luaL_optinteger(L, startIdx + i * 2, 0));
        uint32_t a = static_cast<uint32_t>(luaL_optinteger(L, startIdx + i * 2 + 1, 0));
        if (w > 0) {
            weapons[i] = WeaponSlotData(w, a);
        }
    }
    return weapons;
}

static int Lua_AddPlayerClass(lua_State* L) {
    int skin = static_cast<int>(luaL_checkinteger(L, 1));
    float x = static_cast<float>(luaL_checknumber(L, 2));
    float y = static_cast<float>(luaL_checknumber(L, 3));
    float z = static_cast<float>(luaL_checknumber(L, 4));
    float angle = static_cast<float>(luaL_checknumber(L, 5));
    WeaponSlots weapons = BuildWeaponSlotsFromArgs(L, 6);

    IComponentList* components = ResourceManager::Get().GetComponents();
    IClassesComponent* classes = components ? components->queryComponent<IClassesComponent>() : nullptr;
    if (!classes) { lua_pushinteger(L, -1); return 1; }
    IClass* cls = classes->create(skin, 255, Vector3(x, y, z), angle, weapons);
    lua_pushinteger(L, cls ? cls->getID() : -1);
    return 1;
}

static int Lua_AddPlayerClassEx(lua_State* L) {
    int team = static_cast<int>(luaL_checkinteger(L, 1));
    int skin = static_cast<int>(luaL_checkinteger(L, 2));
    float x = static_cast<float>(luaL_checknumber(L, 3));
    float y = static_cast<float>(luaL_checknumber(L, 4));
    float z = static_cast<float>(luaL_checknumber(L, 5));
    float angle = static_cast<float>(luaL_checknumber(L, 6));
    WeaponSlots weapons = BuildWeaponSlotsFromArgs(L, 7);

    IComponentList* components = ResourceManager::Get().GetComponents();
    IClassesComponent* classes = components ? components->queryComponent<IClassesComponent>() : nullptr;
    if (!classes) { lua_pushinteger(L, -1); return 1; }
    IClass* cls = classes->create(skin, team, Vector3(x, y, z), angle, weapons);
    lua_pushinteger(L, cls ? cls->getID() : -1);
    return 1;
}

static int Lua_SetSpawnInfo(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    int team = static_cast<int>(luaL_checkinteger(L, 2));
    int skin = static_cast<int>(luaL_checkinteger(L, 3));
    float x = static_cast<float>(luaL_checknumber(L, 4));
    float y = static_cast<float>(luaL_checknumber(L, 5));
    float z = static_cast<float>(luaL_checknumber(L, 6));
    float angle = static_cast<float>(luaL_checknumber(L, 7));
    WeaponSlots weapons = BuildWeaponSlotsFromArgs(L, 8);

    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;
    IPlayer* player = core->getPlayers().get(playerId);
    if (!player) { lua_pushboolean(L, false); return 1; }
    IPlayerClassData* classData = queryExtension<IPlayerClassData>(player);
    if (classData) {
        classData->setSpawnInfo(PlayerClass(skin, team, Vector3(x, y, z), angle, weapons));
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_ForceClassSelection(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;
    IPlayer* player = core->getPlayers().get(playerId);
    if (player) {
        player->forceClassSelection();
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

// ============================================================================
// 13. Object Movement, Attachment & Per-Player Objects
// ============================================================================

static int Lua_MoveObject(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    float x = static_cast<float>(luaL_checknumber(L, 2));
    float y = static_cast<float>(luaL_checknumber(L, 3));
    float z = static_cast<float>(luaL_checknumber(L, 4));
    float speed = static_cast<float>(luaL_checknumber(L, 5));
    IComponentList* components = ResourceManager::Get().GetComponents();
    IObjectsComponent* objects = components ? components->queryComponent<IObjectsComponent>() : nullptr;
    if (!objects) { lua_pushboolean(L, false); return 1; }
    IObject* obj = objects->get(id);
    if (!obj) { lua_pushboolean(L, false); return 1; }
    Vector3 rot = obj->getRotation().ToEuler();
    float rx = static_cast<float>(luaL_optnumber(L, 6, rot.x));
    float ry = static_cast<float>(luaL_optnumber(L, 7, rot.y));
    float rz = static_cast<float>(luaL_optnumber(L, 8, rot.z));
    ObjectMoveData data;
    data.targetPos = Vector3(x, y, z);
    data.targetRot = Vector3(rx, ry, rz);
    data.speed = speed;
    obj->move(data);
    lua_pushboolean(L, true);
    return 1;
}

static int Lua_StopObject(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    IComponentList* components = ResourceManager::Get().GetComponents();
    IObjectsComponent* objects = components ? components->queryComponent<IObjectsComponent>() : nullptr;
    if (!objects) { lua_pushboolean(L, false); return 1; }
    IObject* obj = objects->get(id);
    if (obj) { obj->stop(); lua_pushboolean(L, true); }
    else { lua_pushboolean(L, false); }
    return 1;
}

static int Lua_IsObjectMoving(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    IComponentList* components = ResourceManager::Get().GetComponents();
    IObjectsComponent* objects = components ? components->queryComponent<IObjectsComponent>() : nullptr;
    if (!objects) { lua_pushboolean(L, false); return 1; }
    IObject* obj = objects->get(id);
    lua_pushboolean(L, obj ? obj->isMoving() : false);
    return 1;
}

static int Lua_AttachObjectToVehicle(lua_State* L) {
    int objId = static_cast<int>(luaL_checkinteger(L, 1));
    int vehId = static_cast<int>(luaL_checkinteger(L, 2));
    float ox = static_cast<float>(luaL_checknumber(L, 3));
    float oy = static_cast<float>(luaL_checknumber(L, 4));
    float oz = static_cast<float>(luaL_checknumber(L, 5));
    float rx = static_cast<float>(luaL_checknumber(L, 6));
    float ry = static_cast<float>(luaL_checknumber(L, 7));
    float rz = static_cast<float>(luaL_checknumber(L, 8));
    IComponentList* components = ResourceManager::Get().GetComponents();
    IObjectsComponent* objects = components ? components->queryComponent<IObjectsComponent>() : nullptr;
    IVehiclesComponent* vehicles = components ? components->queryComponent<IVehiclesComponent>() : nullptr;
    if (!objects || !vehicles) { lua_pushboolean(L, false); return 1; }
    IObject* obj = objects->get(objId);
    IVehicle* veh = vehicles->get(vehId);
    if (obj && veh) {
        obj->attachToVehicle(*veh, Vector3(ox, oy, oz), Vector3(rx, ry, rz));
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_AttachObjectToPlayer(lua_State* L) {
    int objId = static_cast<int>(luaL_checkinteger(L, 1));
    int playerId = static_cast<int>(luaL_checkinteger(L, 2));
    float ox = static_cast<float>(luaL_checknumber(L, 3));
    float oy = static_cast<float>(luaL_checknumber(L, 4));
    float oz = static_cast<float>(luaL_checknumber(L, 5));
    float rx = static_cast<float>(luaL_checknumber(L, 6));
    float ry = static_cast<float>(luaL_checknumber(L, 7));
    float rz = static_cast<float>(luaL_checknumber(L, 8));
    ICore* core = ResourceManager::Get().GetCore();
    IComponentList* components = ResourceManager::Get().GetComponents();
    IObjectsComponent* objects = components ? components->queryComponent<IObjectsComponent>() : nullptr;
    if (!core || !objects) { lua_pushboolean(L, false); return 1; }
    IObject* obj = objects->get(objId);
    IPlayer* player = core->getPlayers().get(playerId);
    if (obj && player) {
        obj->attachToPlayer(*player, Vector3(ox, oy, oz), Vector3(rx, ry, rz));
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_CreatePlayerObject(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    int modelId = static_cast<int>(luaL_checkinteger(L, 2));
    float x = static_cast<float>(luaL_checknumber(L, 3));
    float y = static_cast<float>(luaL_checknumber(L, 4));
    float z = static_cast<float>(luaL_checknumber(L, 5));
    float rx = static_cast<float>(luaL_checknumber(L, 6));
    float ry = static_cast<float>(luaL_checknumber(L, 7));
    float rz = static_cast<float>(luaL_checknumber(L, 8));
    float drawDist = static_cast<float>(luaL_optnumber(L, 9, 0.0));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;
    IPlayer* player = core->getPlayers().get(playerId);
    if (!player) { lua_pushinteger(L, -1); return 1; }
    IPlayerObjectData* pod = queryExtension<IPlayerObjectData>(player);
    IPlayerObject* obj = pod ? pod->create(modelId, Vector3(x, y, z), Vector3(rx, ry, rz), drawDist) : nullptr;
    lua_pushinteger(L, obj ? obj->getID() : -1);
    return 1;
}

static int Lua_DestroyPlayerObject(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    int objId = static_cast<int>(luaL_checkinteger(L, 2));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;
    IPlayer* player = core->getPlayers().get(playerId);
    if (!player) { lua_pushboolean(L, false); return 1; }
    IPlayerObjectData* pod = queryExtension<IPlayerObjectData>(player);
    if (pod && pod->get(objId)) {
        pod->release(objId);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_SetPlayerObjectPos(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    int objId = static_cast<int>(luaL_checkinteger(L, 2));
    float x = static_cast<float>(luaL_checknumber(L, 3));
    float y = static_cast<float>(luaL_checknumber(L, 4));
    float z = static_cast<float>(luaL_checknumber(L, 5));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;
    IPlayer* player = core->getPlayers().get(playerId);
    IPlayerObjectData* pod = player ? queryExtension<IPlayerObjectData>(player) : nullptr;
    IPlayerObject* obj = pod ? pod->get(objId) : nullptr;
    if (obj) { obj->setPosition(Vector3(x, y, z)); lua_pushboolean(L, true); }
    else { lua_pushboolean(L, false); }
    return 1;
}

static int Lua_GetPlayerObjectPos(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    int objId = static_cast<int>(luaL_checkinteger(L, 2));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;
    IPlayer* player = core->getPlayers().get(playerId);
    IPlayerObjectData* pod = player ? queryExtension<IPlayerObjectData>(player) : nullptr;
    IPlayerObject* obj = pod ? pod->get(objId) : nullptr;
    if (obj) {
        Vector3 pos = obj->getPosition();
        lua_pushnumber(L, pos.x);
        lua_pushnumber(L, pos.y);
        lua_pushnumber(L, pos.z);
        return 3;
    }
    return 0;
}

static int Lua_SetPlayerObjectRot(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    int objId = static_cast<int>(luaL_checkinteger(L, 2));
    float rx = static_cast<float>(luaL_checknumber(L, 3));
    float ry = static_cast<float>(luaL_checknumber(L, 4));
    float rz = static_cast<float>(luaL_checknumber(L, 5));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;
    IPlayer* player = core->getPlayers().get(playerId);
    IPlayerObjectData* pod = player ? queryExtension<IPlayerObjectData>(player) : nullptr;
    IPlayerObject* obj = pod ? pod->get(objId) : nullptr;
    if (obj) { obj->setRotation(Vector3(rx, ry, rz)); lua_pushboolean(L, true); }
    else { lua_pushboolean(L, false); }
    return 1;
}

static int Lua_GetPlayerObjectRot(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    int objId = static_cast<int>(luaL_checkinteger(L, 2));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;
    IPlayer* player = core->getPlayers().get(playerId);
    IPlayerObjectData* pod = player ? queryExtension<IPlayerObjectData>(player) : nullptr;
    IPlayerObject* obj = pod ? pod->get(objId) : nullptr;
    if (obj) {
        Vector3 rot = obj->getRotation().ToEuler();
        lua_pushnumber(L, rot.x);
        lua_pushnumber(L, rot.y);
        lua_pushnumber(L, rot.z);
        return 3;
    }
    return 0;
}

static int Lua_MovePlayerObject(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    int objId = static_cast<int>(luaL_checkinteger(L, 2));
    float x = static_cast<float>(luaL_checknumber(L, 3));
    float y = static_cast<float>(luaL_checknumber(L, 4));
    float z = static_cast<float>(luaL_checknumber(L, 5));
    float speed = static_cast<float>(luaL_checknumber(L, 6));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;
    IPlayer* player = core->getPlayers().get(playerId);
    IPlayerObjectData* pod = player ? queryExtension<IPlayerObjectData>(player) : nullptr;
    IPlayerObject* obj = pod ? pod->get(objId) : nullptr;
    if (!obj) { lua_pushboolean(L, false); return 1; }
    Vector3 rot = obj->getRotation().ToEuler();
    float rx = static_cast<float>(luaL_optnumber(L, 7, rot.x));
    float ry = static_cast<float>(luaL_optnumber(L, 8, rot.y));
    float rz = static_cast<float>(luaL_optnumber(L, 9, rot.z));
    ObjectMoveData data;
    data.targetPos = Vector3(x, y, z);
    data.targetRot = Vector3(rx, ry, rz);
    data.speed = speed;
    obj->move(data);
    lua_pushboolean(L, true);
    return 1;
}

static int Lua_StopPlayerObject(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    int objId = static_cast<int>(luaL_checkinteger(L, 2));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;
    IPlayer* player = core->getPlayers().get(playerId);
    IPlayerObjectData* pod = player ? queryExtension<IPlayerObjectData>(player) : nullptr;
    IPlayerObject* obj = pod ? pod->get(objId) : nullptr;
    if (obj) { obj->stop(); lua_pushboolean(L, true); }
    else { lua_pushboolean(L, false); }
    return 1;
}

static int Lua_IsPlayerObjectMoving(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    int objId = static_cast<int>(luaL_checkinteger(L, 2));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;
    IPlayer* player = core->getPlayers().get(playerId);
    IPlayerObjectData* pod = player ? queryExtension<IPlayerObjectData>(player) : nullptr;
    IPlayerObject* obj = pod ? pod->get(objId) : nullptr;
    lua_pushboolean(L, obj ? obj->isMoving() : false);
    return 1;
}

// ============================================================================
// 14. Per-Player 3D Text Labels & Label Attachment
// ============================================================================

static int Lua_Attach3DTextLabelToPlayer(lua_State* L) {
    int labelId = static_cast<int>(luaL_checkinteger(L, 1));
    int playerId = static_cast<int>(luaL_checkinteger(L, 2));
    float ox = static_cast<float>(luaL_checknumber(L, 3));
    float oy = static_cast<float>(luaL_checknumber(L, 4));
    float oz = static_cast<float>(luaL_checknumber(L, 5));
    ICore* core = ResourceManager::Get().GetCore();
    IComponentList* components = ResourceManager::Get().GetComponents();
    ITextLabelsComponent* labels = components ? components->queryComponent<ITextLabelsComponent>() : nullptr;
    if (!core || !labels) { lua_pushboolean(L, false); return 1; }
    ITextLabel* label = labels->get(labelId);
    IPlayer* player = core->getPlayers().get(playerId);
    if (label && player) {
        label->attachToPlayer(*player, Vector3(ox, oy, oz));
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_Attach3DTextLabelToVehicle(lua_State* L) {
    int labelId = static_cast<int>(luaL_checkinteger(L, 1));
    int vehId = static_cast<int>(luaL_checkinteger(L, 2));
    float ox = static_cast<float>(luaL_checknumber(L, 3));
    float oy = static_cast<float>(luaL_checknumber(L, 4));
    float oz = static_cast<float>(luaL_checknumber(L, 5));
    IComponentList* components = ResourceManager::Get().GetComponents();
    ITextLabelsComponent* labels = components ? components->queryComponent<ITextLabelsComponent>() : nullptr;
    IVehiclesComponent* vehicles = components ? components->queryComponent<IVehiclesComponent>() : nullptr;
    if (!labels || !vehicles) { lua_pushboolean(L, false); return 1; }
    ITextLabel* label = labels->get(labelId);
    IVehicle* veh = vehicles->get(vehId);
    if (label && veh) {
        label->attachToVehicle(*veh, Vector3(ox, oy, oz));
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_CreatePlayer3DTextLabel(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    const char* text = luaL_checkstring(L, 2);
    uint32_t color = static_cast<uint32_t>(luaL_checkinteger(L, 3));
    float x = static_cast<float>(luaL_checknumber(L, 4));
    float y = static_cast<float>(luaL_checknumber(L, 5));
    float z = static_cast<float>(luaL_checknumber(L, 6));
    float drawDist = static_cast<float>(luaL_checknumber(L, 7));
    int attachedPlayerId = static_cast<int>(luaL_optinteger(L, 8, -1));
    int attachedVehicleId = static_cast<int>(luaL_optinteger(L, 9, -1));
    bool testLOS = lua_toboolean(L, 10) != 0;

    ICore* core = ResourceManager::Get().GetCore();
    IComponentList* components = ResourceManager::Get().GetComponents();
    if (!core) return 0;
    IPlayer* player = core->getPlayers().get(playerId);
    IPlayerTextLabelData* ptd = player ? queryExtension<IPlayerTextLabelData>(player) : nullptr;
    if (!ptd) { lua_pushinteger(L, -1); return 1; }

    IPlayerTextLabel* label = nullptr;
    if (attachedPlayerId >= 0) {
        IPlayer* target = core->getPlayers().get(attachedPlayerId);
        if (target) label = ptd->create(text, Colour::FromRGBA(color), Vector3(x, y, z), drawDist, testLOS, *target);
    } else if (attachedVehicleId >= 0) {
        IVehiclesComponent* vehicles = components ? components->queryComponent<IVehiclesComponent>() : nullptr;
        IVehicle* veh = vehicles ? vehicles->get(attachedVehicleId) : nullptr;
        if (veh) label = ptd->create(text, Colour::FromRGBA(color), Vector3(x, y, z), drawDist, testLOS, *veh);
    } else {
        label = ptd->create(text, Colour::FromRGBA(color), Vector3(x, y, z), drawDist, testLOS);
    }
    lua_pushinteger(L, label ? label->getID() : -1);
    return 1;
}

static int Lua_DestroyPlayer3DTextLabel(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    int labelId = static_cast<int>(luaL_checkinteger(L, 2));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;
    IPlayer* player = core->getPlayers().get(playerId);
    IPlayerTextLabelData* ptd = player ? queryExtension<IPlayerTextLabelData>(player) : nullptr;
    if (ptd && ptd->get(labelId)) {
        ptd->release(labelId);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_UpdatePlayer3DTextLabelText(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    int labelId = static_cast<int>(luaL_checkinteger(L, 2));
    uint32_t color = static_cast<uint32_t>(luaL_checkinteger(L, 3));
    const char* text = luaL_checkstring(L, 4);
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;
    IPlayer* player = core->getPlayers().get(playerId);
    IPlayerTextLabelData* ptd = player ? queryExtension<IPlayerTextLabelData>(player) : nullptr;
    IPlayerTextLabel* label = ptd ? ptd->get(labelId) : nullptr;
    if (label) {
        label->setColourAndText(Colour::FromRGBA(color), text);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

// ============================================================================
// 15. Player Gameplay, Status, HUD & Admin
// ============================================================================

static int Lua_TogglePlayerControllable(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    bool toggle = lua_toboolean(L, 2) != 0;
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;
    IPlayer* player = core->getPlayers().get(playerId);
    if (player) { player->setControllable(toggle); lua_pushboolean(L, true); }
    else { lua_pushboolean(L, false); }
    return 1;
}

static int Lua_GameTextForPlayer(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    const char* text = luaL_checkstring(L, 2);
    int timeMs = static_cast<int>(luaL_checkinteger(L, 3));
    int style = static_cast<int>(luaL_checkinteger(L, 4));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;
    IPlayer* player = core->getPlayers().get(playerId);
    if (player) {
        player->sendGameText(text, Milliseconds(timeMs), style);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_GameTextForAll(lua_State* L) {
    const char* text = luaL_checkstring(L, 1);
    int timeMs = static_cast<int>(luaL_checkinteger(L, 2));
    int style = static_cast<int>(luaL_checkinteger(L, 3));
    ICore* core = ResourceManager::Get().GetCore();
    if (core) {
        core->getPlayers().sendGameTextToAll(text, Milliseconds(timeMs), style);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_SendDeathMessage(lua_State* L) {
    int killerId = static_cast<int>(luaL_checkinteger(L, 1));
    int killeeId = static_cast<int>(luaL_checkinteger(L, 2));
    int weapon = static_cast<int>(luaL_checkinteger(L, 3));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;
    IPlayer* killee = core->getPlayers().get(killeeId);
    IPlayer* killer = (killerId >= 0) ? core->getPlayers().get(killerId) : nullptr;
    if (killee) {
        for (IPlayer* p : core->getPlayers().entries()) {
            p->sendDeathMessage(*killee, killer, weapon);
        }
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_SendDeathMessageToAll(lua_State* L) {
    int killerId = static_cast<int>(luaL_checkinteger(L, 1));
    int killeeId = static_cast<int>(luaL_checkinteger(L, 2));
    int weapon = static_cast<int>(luaL_checkinteger(L, 3));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;
    IPlayer* killee = core->getPlayers().get(killeeId);
    IPlayer* killer = (killerId >= 0) ? core->getPlayers().get(killerId) : nullptr;
    if (killee) {
        core->getPlayers().sendDeathMessageToAll(killer, *killee, weapon);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_ShowPlayerNameTagForPlayer(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    int showPlayerId = static_cast<int>(luaL_checkinteger(L, 2));
    bool show = lua_toboolean(L, 3) != 0;
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;
    IPlayer* player = core->getPlayers().get(playerId);
    IPlayer* other = core->getPlayers().get(showPlayerId);
    if (player && other) {
        player->toggleOtherNameTag(*other, show);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_SetPlayerWantedLevel(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    unsigned level = static_cast<unsigned>(luaL_checkinteger(L, 2));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;
    IPlayer* player = core->getPlayers().get(playerId);
    if (player) { player->setWantedLevel(level); lua_pushboolean(L, true); }
    else { lua_pushboolean(L, false); }
    return 1;
}

static int Lua_GetPlayerWantedLevel(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;
    IPlayer* player = core->getPlayers().get(playerId);
    lua_pushinteger(L, player ? static_cast<int>(player->getWantedLevel()) : 0);
    return 1;
}

static int Lua_SetPlayerDrunkLevel(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    int level = static_cast<int>(luaL_checkinteger(L, 2));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;
    IPlayer* player = core->getPlayers().get(playerId);
    if (player) { player->setDrunkLevel(level); lua_pushboolean(L, true); }
    else { lua_pushboolean(L, false); }
    return 1;
}

static int Lua_GetPlayerDrunkLevel(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;
    IPlayer* player = core->getPlayers().get(playerId);
    lua_pushinteger(L, player ? player->getDrunkLevel() : 0);
    return 1;
}

static int Lua_SetPlayerColor(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    uint32_t color = static_cast<uint32_t>(luaL_checkinteger(L, 2));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;
    IPlayer* player = core->getPlayers().get(playerId);
    if (player) { player->setColour(Colour::FromRGBA(color)); lua_pushboolean(L, true); }
    else { lua_pushboolean(L, false); }
    return 1;
}

static int Lua_GetPlayerColor(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;
    IPlayer* player = core->getPlayers().get(playerId);
    lua_pushinteger(L, player ? static_cast<lua_Integer>(player->getColour().RGBA()) : 0);
    return 1;
}

static int Lua_SetPlayerTeam(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    int team = static_cast<int>(luaL_checkinteger(L, 2));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;
    IPlayer* player = core->getPlayers().get(playerId);
    if (player) { player->setTeam(team); lua_pushboolean(L, true); }
    else { lua_pushboolean(L, false); }
    return 1;
}

static int Lua_GetPlayerTeam(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;
    IPlayer* player = core->getPlayers().get(playerId);
    lua_pushinteger(L, player ? player->getTeam() : 255);
    return 1;
}

static int Lua_SetPlayerSkillLevel(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    int skill = static_cast<int>(luaL_checkinteger(L, 2));
    int level = static_cast<int>(luaL_checkinteger(L, 3));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;
    IPlayer* player = core->getPlayers().get(playerId);
    if (player) {
        player->setSkillLevel(static_cast<PlayerWeaponSkill>(skill), level);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_GetPlayerState(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;
    IPlayer* player = core->getPlayers().get(playerId);
    lua_pushinteger(L, player ? static_cast<int>(player->getState()) : PlayerState_None);
    return 1;
}

static int Lua_GetPlayerKeys(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;
    IPlayer* player = core->getPlayers().get(playerId);
    if (player) {
        const PlayerKeyData& kd = player->getKeyData();
        lua_pushinteger(L, static_cast<lua_Integer>(kd.keys));
        lua_pushinteger(L, static_cast<lua_Integer>(kd.upDown));
        lua_pushinteger(L, static_cast<lua_Integer>(kd.leftRight));
        return 3;
    }
    return 0;
}

static int Lua_Ban(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;
    IPlayer* player = core->getPlayers().get(playerId);
    if (player) { player->ban(); lua_pushboolean(L, true); }
    else { lua_pushboolean(L, false); }
    return 1;
}

static int Lua_BanEx(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    const char* reason = luaL_checkstring(L, 2);
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;
    IPlayer* player = core->getPlayers().get(playerId);
    if (player) { player->ban(reason); lua_pushboolean(L, true); }
    else { lua_pushboolean(L, false); }
    return 1;
}

static int Lua_GetPlayerIp(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;
    IPlayer* player = core->getPlayers().get(playerId);
    if (player) {
        PeerAddress::AddressString addr;
        if (PeerAddress::ToString(player->getNetworkData().networkID.address, addr)) {
            lua_pushlstring(L, addr.data(), addr.length());
            return 1;
        }
    }
    lua_pushstring(L, "");
    return 1;
}

static int Lua_GetPlayerSerial(lua_State* L) {
    int playerId = static_cast<int>(luaL_checkinteger(L, 1));
    ICore* core = ResourceManager::Get().GetCore();
    if (!core) return 0;
    IPlayer* player = core->getPlayers().get(playerId);
    if (player) {
        StringView serial = player->getSerial();
        lua_pushlstring(L, serial.data(), serial.length());
    } else {
        lua_pushstring(L, "");
    }
    return 1;
}

// ============================================================================
// Registration Entry Point
// ============================================================================

void RegisterBindings(lua_State* L, Resource* resource) {
    // Store resource pointer in Lua registry
    lua_pushstring(L, "__resa_resource");
    lua_pushlightuserdata(L, resource);
    lua_settable(L, LUA_REGISTRYINDEX);

    // Override print
    lua_register(L, "print", Lua_Print);

    // Core Event & Command Bus
    lua_register(L, "AddEventHandler", Lua_AddEventHandler);
    lua_register(L, "TriggerEvent", Lua_TriggerEvent);
    lua_register(L, "RegisterCommand", Lua_RegisterCommand);

    // Timers
    lua_register(L, "SetTimer", Lua_SetTimer);
    lua_register(L, "SetTimeout", Lua_SetTimeout);
    lua_register(L, "KillTimer", Lua_KillTimer);

    // Internal export helpers
    lua_register(L, "__RegisterExport", Lua_RegisterExport);
    lua_register(L, "__CallExport", Lua_CallExport);

    // Players
    lua_register(L, "GetPlayerName", Lua_GetPlayerName);
    lua_register(L, "GetPlayerPos", Lua_GetPlayerPos);
    lua_register(L, "SetPlayerPos", Lua_SetPlayerPos);
    lua_register(L, "GetPlayerFacingAngle", Lua_GetPlayerFacingAngle);
    lua_register(L, "SetPlayerFacingAngle", Lua_SetPlayerFacingAngle);
    lua_register(L, "GetPlayerHealth", Lua_GetPlayerHealth);
    lua_register(L, "SetPlayerHealth", Lua_SetPlayerHealth);
    lua_register(L, "GetPlayerArmour", Lua_GetPlayerArmour);
    lua_register(L, "SetPlayerArmour", Lua_SetPlayerArmour);
    lua_register(L, "GetPlayerSkin", Lua_GetPlayerSkin);
    lua_register(L, "SetPlayerSkin", Lua_SetPlayerSkin);
    lua_register(L, "SendClientMessage", Lua_SendClientMessage);
    lua_register(L, "SendClientMessageToAll", Lua_SendClientMessageToAll);
    lua_register(L, "SpawnPlayer", Lua_SpawnPlayer);
    lua_register(L, "KickPlayer", Lua_KickPlayer);
    lua_register(L, "GivePlayerWeapon", Lua_GivePlayerWeapon);
    lua_register(L, "ResetPlayerWeapons", Lua_ResetPlayerWeapons);
    lua_register(L, "GetPlayerWeapon", Lua_GetPlayerWeapon);
    lua_register(L, "GetPlayerAmmo", Lua_GetPlayerAmmo);
    lua_register(L, "SetPlayerArmedWeapon", Lua_SetPlayerArmedWeapon);
    lua_register(L, "GivePlayerMoney", Lua_GivePlayerMoney);
    lua_register(L, "GetPlayerMoney", Lua_GetPlayerMoney);
    lua_register(L, "ResetPlayerMoney", Lua_ResetPlayerMoney);
    lua_register(L, "SetPlayerMoney", Lua_SetPlayerMoney);
    lua_register(L, "SetPlayerInterior", Lua_SetPlayerInterior);
    lua_register(L, "GetPlayerInterior", Lua_GetPlayerInterior);
    lua_register(L, "SetPlayerVirtualWorld", Lua_SetPlayerVirtualWorld);
    lua_register(L, "GetPlayerVirtualWorld", Lua_GetPlayerVirtualWorld);
    lua_register(L, "SetPlayerVelocity", Lua_SetPlayerVelocity);
    lua_register(L, "GetPlayerVelocity", Lua_GetPlayerVelocity);
    lua_register(L, "PutPlayerInVehicle", Lua_PutPlayerInVehicle);
    lua_register(L, "RemovePlayerFromVehicle", Lua_RemovePlayerFromVehicle);
    lua_register(L, "GetPlayerVehicleID", Lua_GetPlayerVehicleID);
    lua_register(L, "GetPlayerVehicleSeat", Lua_GetPlayerVehicleSeat);
    lua_register(L, "IsPlayerInAnyVehicle", Lua_IsPlayerInAnyVehicle);
    lua_register(L, "SetPlayerScore", Lua_SetPlayerScore);
    lua_register(L, "GetPlayerScore", Lua_GetPlayerScore);
    lua_register(L, "GetPlayerPing", Lua_GetPlayerPing);
    lua_register(L, "ApplyAnimation", Lua_ApplyAnimation);
    lua_register(L, "ClearAnimations", Lua_ClearAnimations);

    // Camera & Spectating (Item 1)
    lua_register(L, "SetPlayerCameraPos", Lua_SetPlayerCameraPos);
    lua_register(L, "GetPlayerCameraPos", Lua_GetPlayerCameraPos);
    lua_register(L, "SetPlayerCameraLookAt", Lua_SetPlayerCameraLookAt);
    lua_register(L, "GetPlayerCameraLookAt", Lua_GetPlayerCameraLookAt);
    lua_register(L, "SetCameraBehindPlayer", Lua_SetCameraBehindPlayer);
    lua_register(L, "InterpolateCameraPos", Lua_InterpolateCameraPos);
    lua_register(L, "InterpolateCameraLookAt", Lua_InterpolateCameraLookAt);
    lua_register(L, "TogglePlayerSpectating", Lua_TogglePlayerSpectating);
    lua_register(L, "PlayerSpectatePlayer", Lua_PlayerSpectatePlayer);
    lua_register(L, "PlayerSpectateVehicle", Lua_PlayerSpectateVehicle);

    // Audio & Sounds (Item 2)
    lua_register(L, "PlayAudioStreamForPlayer", Lua_PlayAudioStreamForPlayer);
    lua_register(L, "StopAudioStreamForPlayer", Lua_StopAudioStreamForPlayer);
    lua_register(L, "PlayerPlaySound", Lua_PlayerPlaySound);

    // Character Accessories & Special Actions (Item 3)
    lua_register(L, "SetPlayerAttachedObject", Lua_SetPlayerAttachedObject);
    lua_register(L, "RemovePlayerAttachedObject", Lua_RemovePlayerAttachedObject);
    lua_register(L, "IsPlayerAttachedObjectSlotUsed", Lua_IsPlayerAttachedObjectSlotUsed);
    lua_register(L, "SetPlayerSpecialAction", Lua_SetPlayerSpecialAction);
    lua_register(L, "GetPlayerSpecialAction", Lua_GetPlayerSpecialAction);
    lua_register(L, "SetPlayerFightingStyle", Lua_SetPlayerFightingStyle);
    lua_register(L, "GetPlayerFightingStyle", Lua_GetPlayerFightingStyle);

    // Checkpoints & Map Icons (Item 4)
    lua_register(L, "SetPlayerCheckpoint", Lua_SetPlayerCheckpoint);
    lua_register(L, "DisablePlayerCheckpoint", Lua_DisablePlayerCheckpoint);
    lua_register(L, "SetPlayerRaceCheckpoint", Lua_SetPlayerRaceCheckpoint);
    lua_register(L, "DisablePlayerRaceCheckpoint", Lua_DisablePlayerRaceCheckpoint);
    lua_register(L, "SetPlayerMapIcon", Lua_SetPlayerMapIcon);
    lua_register(L, "RemovePlayerMapIcon", Lua_RemovePlayerMapIcon);

    // Vehicles (Item 6)
    lua_register(L, "CreateVehicle", Lua_CreateVehicle);
    lua_register(L, "DestroyVehicle", Lua_DestroyVehicle);
    lua_register(L, "SetVehiclePos", Lua_SetVehiclePos);
    lua_register(L, "GetVehiclePos", Lua_GetVehiclePos);
    lua_register(L, "SetVehicleZAngle", Lua_SetVehicleZAngle);
    lua_register(L, "GetVehicleZAngle", Lua_GetVehicleZAngle);
    lua_register(L, "SetVehicleHealth", Lua_SetVehicleHealth);
    lua_register(L, "GetVehicleHealth", Lua_GetVehicleHealth);
    lua_register(L, "RepairVehicle", Lua_RepairVehicle);
    lua_register(L, "SetVehicleVelocity", Lua_SetVehicleVelocity);
    lua_register(L, "GetVehicleVelocity", Lua_GetVehicleVelocity);
    lua_register(L, "SetVehicleParamsEx", Lua_SetVehicleParamsEx);
    lua_register(L, "GetVehicleParamsEx", Lua_GetVehicleParamsEx);
    lua_register(L, "ChangeVehicleColor", Lua_ChangeVehicleColor);
    lua_register(L, "ChangeVehiclePaintjob", Lua_ChangeVehiclePaintjob);
    lua_register(L, "SetVehicleVirtualWorld", Lua_SetVehicleVirtualWorld);
    lua_register(L, "GetVehicleVirtualWorld", Lua_GetVehicleVirtualWorld);
    lua_register(L, "AddVehicleComponent", Lua_AddVehicleComponent);
    lua_register(L, "RemoveVehicleComponent", Lua_RemoveVehicleComponent);
    lua_register(L, "GetVehicleComponentInSlot", Lua_GetVehicleComponentInSlot);
    lua_register(L, "SetVehicleNumberPlate", Lua_SetVehicleNumberPlate);
    lua_register(L, "SetVehicleToRespawn", Lua_SetVehicleToRespawn);
    lua_register(L, "AttachTrailerToVehicle", Lua_AttachTrailerToVehicle);
    lua_register(L, "DetachTrailerFromVehicle", Lua_DetachTrailerFromVehicle);
    lua_register(L, "IsTrailerAttachedToVehicle", Lua_IsTrailerAttachedToVehicle);
    lua_register(L, "GetVehicleTrailer", Lua_GetVehicleTrailer);

    // World & Environment (Item 7)
    lua_register(L, "SetWorldTime", Lua_SetWorldTime);
    lua_register(L, "SetPlayerTime", Lua_SetPlayerTime);
    lua_register(L, "GetPlayerTime", Lua_GetPlayerTime);
    lua_register(L, "SetWeather", Lua_SetWeather);
    lua_register(L, "SetPlayerWeather", Lua_SetPlayerWeather);
    lua_register(L, "GetPlayerWeather", Lua_GetPlayerWeather);
    lua_register(L, "SetGravity", Lua_SetGravity);
    lua_register(L, "GetGravity", Lua_GetGravity);
    lua_register(L, "CreateExplosion", Lua_CreateExplosion);
    lua_register(L, "CreateExplosionForPlayer", Lua_CreateExplosionForPlayer);
    lua_register(L, "UsePlayerPedAnims", Lua_UsePlayerPedAnims);

    // Static NPC / Actors (Item 9)
    lua_register(L, "CreateActor", Lua_CreateActor);
    lua_register(L, "DestroyActor", Lua_DestroyActor);
    lua_register(L, "SetActorPos", Lua_SetActorPos);
    lua_register(L, "GetActorPos", Lua_GetActorPos);
    lua_register(L, "SetActorFacingAngle", Lua_SetActorFacingAngle);
    lua_register(L, "GetActorFacingAngle", Lua_GetActorFacingAngle);
    lua_register(L, "SetActorHealth", Lua_SetActorHealth);
    lua_register(L, "GetActorHealth", Lua_GetActorHealth);
    lua_register(L, "SetActorInvulnerable", Lua_SetActorInvulnerable);
    lua_register(L, "IsActorInvulnerable", Lua_IsActorInvulnerable);
    lua_register(L, "ApplyActorAnimation", Lua_ApplyActorAnimation);
    lua_register(L, "ClearActorAnimations", Lua_ClearActorAnimations);

    // Dialogs
    lua_register(L, "ShowPlayerDialog", Lua_ShowPlayerDialog);
    lua_register(L, "HidePlayerDialog", Lua_HidePlayerDialog);
    lua_register(L, "GetPlayerDialog", Lua_GetPlayerDialog);
    lua_register(L, "GetPlayerDialogData", Lua_GetPlayerDialogData);

    // Pickups
    lua_register(L, "CreatePickup", Lua_CreatePickup);
    lua_register(L, "DestroyPickup", Lua_DestroyPickup);

    // 3D Text Labels
    lua_register(L, "Create3DTextLabel", Lua_Create3DTextLabel);
    lua_register(L, "Delete3DTextLabel", Lua_Delete3DTextLabel);
    lua_register(L, "Update3DTextLabelText", Lua_Update3DTextLabelText);

    // Objects
    lua_register(L, "CreateObject", Lua_CreateObject);
    lua_register(L, "DestroyObject", Lua_DestroyObject);
    lua_register(L, "SetObjectPos", Lua_SetObjectPos);
    lua_register(L, "GetObjectPos", Lua_GetObjectPos);
    lua_register(L, "SetObjectRot", Lua_SetObjectRot);
    lua_register(L, "GetObjectRot", Lua_GetObjectRot);
    lua_register(L, "MoveObject", Lua_MoveObject);
    lua_register(L, "StopObject", Lua_StopObject);
    lua_register(L, "IsObjectMoving", Lua_IsObjectMoving);
    lua_register(L, "AttachObjectToVehicle", Lua_AttachObjectToVehicle);
    lua_register(L, "AttachObjectToPlayer", Lua_AttachObjectToPlayer);
    lua_register(L, "CreatePlayerObject", Lua_CreatePlayerObject);
    lua_register(L, "DestroyPlayerObject", Lua_DestroyPlayerObject);
    lua_register(L, "SetPlayerObjectPos", Lua_SetPlayerObjectPos);
    lua_register(L, "GetPlayerObjectPos", Lua_GetPlayerObjectPos);
    lua_register(L, "SetPlayerObjectRot", Lua_SetPlayerObjectRot);
    lua_register(L, "GetPlayerObjectRot", Lua_GetPlayerObjectRot);
    lua_register(L, "MovePlayerObject", Lua_MovePlayerObject);
    lua_register(L, "StopPlayerObject", Lua_StopPlayerObject);
    lua_register(L, "IsPlayerObjectMoving", Lua_IsPlayerObjectMoving);

    // 3D Text Label Attachment & Per-Player 3D Text Labels
    lua_register(L, "Attach3DTextLabelToPlayer", Lua_Attach3DTextLabelToPlayer);
    lua_register(L, "Attach3DTextLabelToVehicle", Lua_Attach3DTextLabelToVehicle);
    lua_register(L, "CreatePlayer3DTextLabel", Lua_CreatePlayer3DTextLabel);
    lua_register(L, "DestroyPlayer3DTextLabel", Lua_DestroyPlayer3DTextLabel);
    lua_register(L, "UpdatePlayer3DTextLabelText", Lua_UpdatePlayer3DTextLabelText);

    // Variables (PVar & SVar)
    lua_register(L, "SetPVarInt", Lua_SetPVarInt);
    lua_register(L, "GetPVarInt", Lua_GetPVarInt);
    lua_register(L, "SetPVarFloat", Lua_SetPVarFloat);
    lua_register(L, "GetPVarFloat", Lua_GetPVarFloat);
    lua_register(L, "SetPVarString", Lua_SetPVarString);
    lua_register(L, "GetPVarString", Lua_GetPVarString);
    lua_register(L, "DeletePVar", Lua_DeletePVar);
    lua_register(L, "GetPVarType", Lua_GetPVarType);
    lua_register(L, "GetPVarNameAtIndex", Lua_GetPVarNameAtIndex);
    lua_register(L, "GetPVarsUpperIndex", Lua_GetPVarsUpperIndex);
    lua_register(L, "SetSVarInt", Lua_SetSVarInt);
    lua_register(L, "GetSVarInt", Lua_GetSVarInt);
    lua_register(L, "SetSVarFloat", Lua_SetSVarFloat);
    lua_register(L, "GetSVarFloat", Lua_GetSVarFloat);
    lua_register(L, "SetSVarString", Lua_SetSVarString);
    lua_register(L, "GetSVarString", Lua_GetSVarString);
    lua_register(L, "DeleteSVar", Lua_DeleteSVar);
    lua_register(L, "GetSVarType", Lua_GetSVarType);
    lua_register(L, "GetSVarNameAtIndex", Lua_GetSVarNameAtIndex);
    lua_register(L, "GetSVarsUpperIndex", Lua_GetSVarsUpperIndex);

    // Classes & Spawn Selection
    lua_register(L, "AddPlayerClass", Lua_AddPlayerClass);
    lua_register(L, "AddPlayerClassEx", Lua_AddPlayerClassEx);
    lua_register(L, "SetSpawnInfo", Lua_SetSpawnInfo);
    lua_register(L, "ForceClassSelection", Lua_ForceClassSelection);

    // Player Gameplay, Status, HUD & Admin
    lua_register(L, "TogglePlayerControllable", Lua_TogglePlayerControllable);
    lua_register(L, "GameTextForPlayer", Lua_GameTextForPlayer);
    lua_register(L, "GameTextForAll", Lua_GameTextForAll);
    lua_register(L, "SendDeathMessage", Lua_SendDeathMessage);
    lua_register(L, "SendDeathMessageToAll", Lua_SendDeathMessageToAll);
    lua_register(L, "ShowPlayerNameTagForPlayer", Lua_ShowPlayerNameTagForPlayer);
    lua_register(L, "SetPlayerWantedLevel", Lua_SetPlayerWantedLevel);
    lua_register(L, "GetPlayerWantedLevel", Lua_GetPlayerWantedLevel);
    lua_register(L, "SetPlayerDrunkLevel", Lua_SetPlayerDrunkLevel);
    lua_register(L, "GetPlayerDrunkLevel", Lua_GetPlayerDrunkLevel);
    lua_register(L, "SetPlayerColor", Lua_SetPlayerColor);
    lua_register(L, "GetPlayerColor", Lua_GetPlayerColor);
    lua_register(L, "SetPlayerTeam", Lua_SetPlayerTeam);
    lua_register(L, "GetPlayerTeam", Lua_GetPlayerTeam);
    lua_register(L, "SetPlayerSkillLevel", Lua_SetPlayerSkillLevel);
    lua_register(L, "GetPlayerState", Lua_GetPlayerState);
    lua_register(L, "GetPlayerKeys", Lua_GetPlayerKeys);
    lua_register(L, "Ban", Lua_Ban);
    lua_register(L, "BanEx", Lua_BanEx);
    lua_register(L, "GetPlayerIp", Lua_GetPlayerIp);
    lua_register(L, "GetPlayerSerial", Lua_GetPlayerSerial);

    // Variable Type constants
    lua_pushinteger(L, VariableType_None); lua_setglobal(L, "PLAYER_VARTYPE_NONE");
    lua_pushinteger(L, VariableType_Int); lua_setglobal(L, "PLAYER_VARTYPE_INT");
    lua_pushinteger(L, VariableType_String); lua_setglobal(L, "PLAYER_VARTYPE_STRING");
    lua_pushinteger(L, VariableType_Float); lua_setglobal(L, "PLAYER_VARTYPE_FLOAT");
    lua_pushinteger(L, VariableType_None); lua_setglobal(L, "SERVER_VARTYPE_NONE");
    lua_pushinteger(L, VariableType_Int); lua_setglobal(L, "SERVER_VARTYPE_INT");
    lua_pushinteger(L, VariableType_String); lua_setglobal(L, "SERVER_VARTYPE_STRING");
    lua_pushinteger(L, VariableType_Float); lua_setglobal(L, "SERVER_VARTYPE_FLOAT");

    // Player State constants
    lua_pushinteger(L, PlayerState_None); lua_setglobal(L, "PLAYER_STATE_NONE");
    lua_pushinteger(L, PlayerState_OnFoot); lua_setglobal(L, "PLAYER_STATE_ONFOOT");
    lua_pushinteger(L, PlayerState_Driver); lua_setglobal(L, "PLAYER_STATE_DRIVER");
    lua_pushinteger(L, PlayerState_Passenger); lua_setglobal(L, "PLAYER_STATE_PASSENGER");
    lua_pushinteger(L, PlayerState_Wasted); lua_setglobal(L, "PLAYER_STATE_WASTED");
    lua_pushinteger(L, PlayerState_Spawned); lua_setglobal(L, "PLAYER_STATE_SPAWNED");
    lua_pushinteger(L, PlayerState_Spectating); lua_setglobal(L, "PLAYER_STATE_SPECTATING");

    // Bullet Hit Type constants
    lua_pushinteger(L, 0); lua_setglobal(L, "BULLET_HIT_TYPE_NONE");
    lua_pushinteger(L, 1); lua_setglobal(L, "BULLET_HIT_TYPE_PLAYER");
    lua_pushinteger(L, 2); lua_setglobal(L, "BULLET_HIT_TYPE_VEHICLE");
    lua_pushinteger(L, 3); lua_setglobal(L, "BULLET_HIT_TYPE_OBJECT");
    lua_pushinteger(L, 4); lua_setglobal(L, "BULLET_HIT_TYPE_PLAYER_OBJECT");

    // Weapon Skill constants
    lua_pushinteger(L, 0); lua_setglobal(L, "WEAPONSKILL_PISTOL");
    lua_pushinteger(L, 1); lua_setglobal(L, "WEAPONSKILL_PISTOL_SILENCED");
    lua_pushinteger(L, 2); lua_setglobal(L, "WEAPONSKILL_DESERT_EAGLE");
    lua_pushinteger(L, 3); lua_setglobal(L, "WEAPONSKILL_SHOTGUN");
    lua_pushinteger(L, 4); lua_setglobal(L, "WEAPONSKILL_SAWNOFF_SHOTGUN");
    lua_pushinteger(L, 5); lua_setglobal(L, "WEAPONSKILL_SPAS12_SHOTGUN");
    lua_pushinteger(L, 6); lua_setglobal(L, "WEAPONSKILL_MICRO_UZI");
    lua_pushinteger(L, 7); lua_setglobal(L, "WEAPONSKILL_MP5");
    lua_pushinteger(L, 8); lua_setglobal(L, "WEAPONSKILL_AK47");
    lua_pushinteger(L, 9); lua_setglobal(L, "WEAPONSKILL_M4");
    lua_pushinteger(L, 10); lua_setglobal(L, "WEAPONSKILL_SNIPERRIFLE");

    // Dialog style constants
    lua_pushinteger(L, DialogStyle_MSGBOX);
    lua_setglobal(L, "DIALOG_STYLE_MSGBOX");
    lua_pushinteger(L, DialogStyle_INPUT);
    lua_setglobal(L, "DIALOG_STYLE_INPUT");
    lua_pushinteger(L, DialogStyle_LIST);
    lua_setglobal(L, "DIALOG_STYLE_LIST");
    lua_pushinteger(L, DialogStyle_PASSWORD);
    lua_setglobal(L, "DIALOG_STYLE_PASSWORD");
    lua_pushinteger(L, DialogStyle_TABLIST);
    lua_setglobal(L, "DIALOG_STYLE_TABLIST");
    lua_pushinteger(L, DialogStyle_TABLIST_HEADERS);
    lua_setglobal(L, "DIALOG_STYLE_TABLIST_HEADERS");

    // Dialog response constants
    lua_pushinteger(L, DialogResponse_Right);
    lua_setglobal(L, "DIALOG_RESPONSE_RIGHT");
    lua_pushinteger(L, DialogResponse_Left);
    lua_setglobal(L, "DIALOG_RESPONSE_LEFT");

    // Weapons constants
    lua_pushinteger(L, 0); lua_setglobal(L, "WEAPON_FIST");
    lua_pushinteger(L, 1); lua_setglobal(L, "WEAPON_BRASSKNUCKLE");
    lua_pushinteger(L, 2); lua_setglobal(L, "WEAPON_GOLFCLUB");
    lua_pushinteger(L, 3); lua_setglobal(L, "WEAPON_NITESTICK");
    lua_pushinteger(L, 4); lua_setglobal(L, "WEAPON_KNIFE");
    lua_pushinteger(L, 5); lua_setglobal(L, "WEAPON_BAT");
    lua_pushinteger(L, 6); lua_setglobal(L, "WEAPON_SHOVEL");
    lua_pushinteger(L, 7); lua_setglobal(L, "WEAPON_POOLSTICK");
    lua_pushinteger(L, 8); lua_setglobal(L, "WEAPON_KATANA");
    lua_pushinteger(L, 9); lua_setglobal(L, "WEAPON_CHAINSAW");
    lua_pushinteger(L, 22); lua_setglobal(L, "WEAPON_COLT45");
    lua_pushinteger(L, 23); lua_setglobal(L, "WEAPON_SILENCED");
    lua_pushinteger(L, 24); lua_setglobal(L, "WEAPON_DEAGLE");
    lua_pushinteger(L, 25); lua_setglobal(L, "WEAPON_SHOTGUN");
    lua_pushinteger(L, 26); lua_setglobal(L, "WEAPON_SAWEDOFF");
    lua_pushinteger(L, 27); lua_setglobal(L, "WEAPON_SHOTGSPA");
    lua_pushinteger(L, 28); lua_setglobal(L, "WEAPON_UZI");
    lua_pushinteger(L, 29); lua_setglobal(L, "WEAPON_MP5");
    lua_pushinteger(L, 30); lua_setglobal(L, "WEAPON_AK47");
    lua_pushinteger(L, 31); lua_setglobal(L, "WEAPON_M4");
    lua_pushinteger(L, 32); lua_setglobal(L, "WEAPON_TEC9");
    lua_pushinteger(L, 33); lua_setglobal(L, "WEAPON_RIFLE");
    lua_pushinteger(L, 34); lua_setglobal(L, "WEAPON_SNIPER");
    lua_pushinteger(L, 35); lua_setglobal(L, "WEAPON_ROCKETLAUNCHER");
    lua_pushinteger(L, 36); lua_setglobal(L, "WEAPON_HEATSEEKER");
    lua_pushinteger(L, 37); lua_setglobal(L, "WEAPON_FLAMETHROWER");
    lua_pushinteger(L, 38); lua_setglobal(L, "WEAPON_MINIGUN");

    // Camera Cut constants
    lua_pushinteger(L, 1); lua_setglobal(L, "CAMERA_CUT");
    lua_pushinteger(L, 2); lua_setglobal(L, "CAMERA_MOVE");

    // Spectate Mode constants
    lua_pushinteger(L, 1); lua_setglobal(L, "SPECTATE_MODE_NORMAL");
    lua_pushinteger(L, 2); lua_setglobal(L, "SPECTATE_MODE_FIXED");
    lua_pushinteger(L, 3); lua_setglobal(L, "SPECTATE_MODE_SIDE");

    // Fighting Style constants
    lua_pushinteger(L, 4); lua_setglobal(L, "FIGHT_STYLE_NORMAL");
    lua_pushinteger(L, 5); lua_setglobal(L, "FIGHT_STYLE_BOXING");
    lua_pushinteger(L, 6); lua_setglobal(L, "FIGHT_STYLE_KUNGFU");
    lua_pushinteger(L, 7); lua_setglobal(L, "FIGHT_STYLE_KNEEHEAD");
    lua_pushinteger(L, 15); lua_setglobal(L, "FIGHT_STYLE_GRABKICK");
    lua_pushinteger(L, 16); lua_setglobal(L, "FIGHT_STYLE_ELBOW");

    // Special Action constants
    lua_pushinteger(L, 0); lua_setglobal(L, "SPECIAL_ACTION_NONE");
    lua_pushinteger(L, 1); lua_setglobal(L, "SPECIAL_ACTION_DUCK");
    lua_pushinteger(L, 2); lua_setglobal(L, "SPECIAL_ACTION_USEJETPACK");
    lua_pushinteger(L, 10); lua_setglobal(L, "SPECIAL_ACTION_HANDSUP");
    lua_pushinteger(L, 11); lua_setglobal(L, "SPECIAL_ACTION_USECELLPHONE");
    lua_pushinteger(L, 20); lua_setglobal(L, "SPECIAL_ACTION_DRINK_BEER");
    lua_pushinteger(L, 21); lua_setglobal(L, "SPECIAL_ACTION_SMOKE_CIGGY");
    lua_pushinteger(L, 22); lua_setglobal(L, "SPECIAL_ACTION_DRINK_WINE");
    lua_pushinteger(L, 23); lua_setglobal(L, "SPECIAL_ACTION_DRINK_SPRUNK");
    lua_pushinteger(L, 24); lua_setglobal(L, "SPECIAL_ACTION_CUFFED");

    // Race Checkpoint constants
    lua_pushinteger(L, 0); lua_setglobal(L, "RACE_CHECKPOINT_NORMAL");
    lua_pushinteger(L, 1); lua_setglobal(L, "RACE_CHECKPOINT_FINISH");
    lua_pushinteger(L, 2); lua_setglobal(L, "RACE_CHECKPOINT_NOTHING");
    lua_pushinteger(L, 3); lua_setglobal(L, "RACE_CHECKPOINT_AIR_NORMAL");
    lua_pushinteger(L, 4); lua_setglobal(L, "RACE_CHECKPOINT_AIR_FINISH");

    // Initialize full FiveM-style exports table with metatable lookup
    const char* exportsDsl = R"lua(
        local _exports = {}
        local exportsMeta = {
            __index = function(tbl, resName)
                local resProxy = {}
                setmetatable(resProxy, {
                    __index = function(_, fnName)
                        return function(...)
                            return __CallExport(resName, fnName, ...)
                        end
                    end
                })
                return resProxy
            end,
            __call = function(tbl, fnName, fn)
                __RegisterExport(fnName, fn)
            end
        }
        setmetatable(_exports, exportsMeta)
        exports = _exports
    )lua";
    luaL_dostring(L, exportsDsl);
}

} // namespace Resa
