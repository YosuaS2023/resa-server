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
#include <sstream>

namespace Resa {

// Helper: Custom print redirecting to Resa/OpenMP log
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

// AddEventHandler(eventName, callbackFunction)
static int Lua_AddEventHandler(lua_State* L) {
    const char* eventName = luaL_checkstring(L, 1);
    if (!lua_isfunction(L, 2)) {
        return luaL_error(L, "AddEventHandler expected function as 2nd argument");
    }

    lua_pushvalue(L, 2);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);

    // Retrieve current Resource from registry
    lua_pushstring(L, "__resa_resource");
    lua_gettable(L, LUA_REGISTRYINDEX);
    Resource* res = static_cast<Resource*>(lua_touserdata(L, -1));
    lua_pop(L, 1);

    if (res) {
        EventManager::Get().AddListener(eventName, res, ref);
    }
    return 0;
}

// TriggerEvent(eventName, ...)
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

// Player API
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

// Vehicle API
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

// Dialog API
static int Lua_ShowPlayerDialog(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    int dialogId = static_cast<int>(luaL_checkinteger(L, 2));
    int style = static_cast<int>(luaL_checkinteger(L, 3));
    const char* title = luaL_checkstring(L, 4);
    const char* body = luaL_checkstring(L, 5);
    const char* button1 = luaL_checkstring(L, 6);
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

void RegisterBindings(lua_State* L, Resource* resource) {
    // Store resource pointer in Lua registry
    lua_pushstring(L, "__resa_resource");
    lua_pushlightuserdata(L, resource);
    lua_settable(L, LUA_REGISTRYINDEX);

    // Override print
    lua_register(L, "print", Lua_Print);

    // Events
    lua_register(L, "AddEventHandler", Lua_AddEventHandler);
    lua_register(L, "TriggerEvent", Lua_TriggerEvent);

    // Players
    lua_register(L, "GetPlayerName", Lua_GetPlayerName);
    lua_register(L, "GetPlayerPos", Lua_GetPlayerPos);
    lua_register(L, "SetPlayerPos", Lua_SetPlayerPos);
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

    // Vehicles
    lua_register(L, "CreateVehicle", Lua_CreateVehicle);
    lua_register(L, "DestroyVehicle", Lua_DestroyVehicle);
    lua_register(L, "SetVehiclePos", Lua_SetVehiclePos);
    lua_register(L, "GetVehiclePos", Lua_GetVehiclePos);

    // Dialogs
    lua_register(L, "ShowPlayerDialog", Lua_ShowPlayerDialog);
    lua_register(L, "HidePlayerDialog", Lua_HidePlayerDialog);
    lua_register(L, "GetPlayerDialog", Lua_GetPlayerDialog);
    lua_register(L, "GetPlayerDialogData", Lua_GetPlayerDialogData);

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

    // Initialise empty exports table
    lua_newtable(L);
    lua_setglobal(L, "exports");
}

} // namespace Resa
