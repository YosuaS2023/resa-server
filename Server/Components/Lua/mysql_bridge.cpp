/*
 *  resa-multiplayer (resam)
 *  Lua <-> LuaMySQL Component Bridge Implementation
 */

#include "mysql_bridge.hpp"
#include "resource.hpp"
#include "resource_manager.hpp"

#include <Server/Components/LuaMySQL/lua_mysql.hpp>
#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Resa {

namespace {

constexpr const char* MYSQL_CONN_MT = "ResaMySQLConnection";

struct LuaMySQLConn {
    int handle = 0;
};

using ResultPtr = std::shared_ptr<IMySQLResult>;

ResultPtr WrapResult(IMySQLResult* raw, bool addRef = false) {
    if (!raw) return nullptr;
    if (addRef) raw->retain();
    return ResultPtr(raw, [](IMySQLResult* r) {
        if (r) r->release();
    });
}

ILuaMySQLComponent* GetMySQL() {
    IComponentList* cl = ResourceManager::Get().GetComponents();
    return cl ? cl->queryComponent<ILuaMySQLComponent>() : nullptr;
}

Resource* GetLuaResource(lua_State* L) {
    lua_pushstring(L, "__resa_resource");
    lua_gettable(L, LUA_REGISTRYINDEX);
    Resource* res = static_cast<Resource*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return res;
}

ResultPtr g_ActiveCache;
int g_NextCacheId = 1;
std::unordered_map<int, ResultPtr> g_SavedCaches;

void UnrefRefs(lua_State* L, int callbackRef, const std::vector<int>& argRefs) {
    if (!L) return;
    if (callbackRef != LUA_NOREF && callbackRef != LUA_REFNIL) {
        luaL_unref(L, LUA_REGISTRYINDEX, callbackRef);
    }
    for (int ref : argRefs) {
        if (ref != LUA_NOREF && ref != LUA_REFNIL) {
            luaL_unref(L, LUA_REGISTRYINDEX, ref);
        }
    }
}

void PushResultTables(lua_State* L, IMySQLResult* res) {
    if (!res || !res->isOk()) {
        lua_pushnil(L);
    } else {
        size_t rowCount = res->getRowCount();
        size_t fieldCount = res->getFieldCount();
        lua_createtable(L, static_cast<int>(rowCount), 0);
        for (size_t r = 0; r < rowCount; ++r) {
            lua_createtable(L, 0, static_cast<int>(fieldCount));
            for (size_t c = 0; c < fieldCount; ++c) {
                StringView fname = res->getFieldName(c);
                lua_pushlstring(L, fname.data(), fname.length());
                if (res->isNull(r, c)) {
                    lua_pushnil(L);
                } else {
                    StringView val = res->getValue(r, c);
                    lua_pushlstring(L, val.data(), val.length());
                }
                lua_rawset(L, -3);
            }
            lua_rawseti(L, -2, static_cast<lua_Integer>(r + 1));
        }
    }

    lua_createtable(L, 0, 6);
    lua_pushboolean(L, res && res->isOk() ? 1 : 0);
    lua_setfield(L, -2, "ok");

    lua_pushinteger(L, res ? static_cast<lua_Integer>(res->getErrno()) : 0);
    lua_setfield(L, -2, "errno");

    StringView err = res ? res->getError() : StringView();
    lua_pushlstring(L, err.data(), err.length());
    lua_setfield(L, -2, "error");

    lua_pushinteger(L, res ? static_cast<lua_Integer>(res->getInsertId()) : 0);
    lua_setfield(L, -2, "insert_id");

    lua_pushinteger(L, res ? static_cast<lua_Integer>(res->getAffectedRows()) : 0);
    lua_setfield(L, -2, "affected_rows");

    lua_pushinteger(L, res ? static_cast<lua_Integer>(res->getRowCount()) : 0);
    lua_setfield(L, -2, "count");
}

class LuaMySQLCallback;
std::unordered_set<LuaMySQLCallback*> g_PendingCallbacks;

class LuaMySQLCallback final : public IMySQLQueryCallback {
public:
    LuaMySQLCallback(Resource* res, int cbRef, std::string cbName, std::vector<int> args, bool oop)
        : resource_(res), callbackRef_(cbRef), callbackName_(std::move(cbName)),
          argRefs_(std::move(args)), oopMode_(oop) {
        g_PendingCallbacks.insert(this);
    }

    void Cancel(lua_State* L) {
        cancelled_ = true;
        UnrefRefs(L, callbackRef_, argRefs_);
        callbackRef_ = LUA_NOREF;
        argRefs_.clear();
        resource_ = nullptr;
    }

    Resource* GetResource() const { return resource_; }

    void onQueryComplete(IMySQLResult* result) override {
        g_PendingCallbacks.erase(this);

        if (!cancelled_ && resource_ && resource_->GetState() == ResourceState::Running) {
            lua_State* L = resource_->GetLua();
            if (L) {
                ResultPtr prevCache = g_ActiveCache;
                g_ActiveCache = WrapResult(result, true);

                if (result && !result->isOk()) {
                    StringView err = result->getError();
                    ResourceManager::Get().LogError(
                        "[LuaMySQL][" + resource_->GetName() + "] Query error (" +
                        std::to_string(result->getErrno()) + "): " +
                        std::string(err.data(), err.length()));
                }

                bool pushedFn = false;
                if (callbackRef_ != LUA_NOREF && callbackRef_ != LUA_REFNIL) {
                    lua_rawgeti(L, LUA_REGISTRYINDEX, callbackRef_);
                    pushedFn = lua_isfunction(L, -1);
                    if (!pushedFn) lua_pop(L, 1);
                } else if (!callbackName_.empty()) {
                    lua_getglobal(L, callbackName_.c_str());
                    pushedFn = lua_isfunction(L, -1);
                    if (!pushedFn) lua_pop(L, 1);
                }

                if (pushedFn) {
                    int nargs = 0;
                    if (oopMode_) {
                        PushResultTables(L, result);
                        nargs = 2;
                    }
                    for (int ref : argRefs_) {
                        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
                        ++nargs;
                    }

                    if (lua_pcall(L, nargs, 0, 0) != LUA_OK) {
                        const char* err = lua_tostring(L, -1);
                        ResourceManager::Get().LogError(
                            "[LuaMySQL Callback Error][" + resource_->GetName() + "]: " +
                            (err ? err : "unknown error"));
                        lua_pop(L, 1);
                    }
                }

                UnrefRefs(L, callbackRef_, argRefs_);
                g_ActiveCache = std::move(prevCache);
            }
        }

        delete this;
    }

private:
    Resource* resource_ = nullptr;
    int callbackRef_ = LUA_NOREF;
    std::string callbackName_;
    std::vector<int> argRefs_;
    bool oopMode_ = false;
    bool cancelled_ = false;
};

int GetHandleArg(lua_State* L, int idx) {
    if (lua_isuserdata(L, idx)) {
        LuaMySQLConn* ud = static_cast<LuaMySQLConn*>(luaL_checkudata(L, idx, MYSQL_CONN_MT));
        return ud ? ud->handle : 0;
    }
    return static_cast<int>(luaL_checkinteger(L, idx));
}

std::string EscapeViaComponent(ILuaMySQLComponent* mysql, int handle, StringView input) {
    std::string out;
    if (!mysql) return std::string(input.data(), input.length());
    mysql->escapeString(
        handle, input,
        [](void* ctx, const char* data, size_t len) {
            static_cast<std::string*>(ctx)->assign(data, len);
        },
        &out);
    return out;
}

std::string FormatMySQLString(lua_State* L, ILuaMySQLComponent* mysql, int handle, int fmtIdx, int firstArgIdx) {
    size_t fmtLen = 0;
    const char* fmt = luaL_checklstring(L, fmtIdx, &fmtLen);
    int nargs = lua_gettop(L);
    int curArg = firstArgIdx;

    std::string out;
    out.reserve(fmtLen + 64);

    for (size_t i = 0; i < fmtLen; ++i) {
        if (fmt[i] != '%' || i + 1 >= fmtLen) {
            out.push_back(fmt[i]);
            continue;
        }
        char spec = fmt[++i];
        if (spec == '%') {
            out.push_back('%');
            continue;
        }
        if (curArg > nargs) {
            out.push_back('%');
            out.push_back(spec);
            continue;
        }
        switch (spec) {
            case 'e': {
                size_t slen = 0;
                const char* s = lua_tolstring(L, curArg++, &slen);
                out += EscapeViaComponent(mysql, handle, StringView(s ? s : "", s ? slen : 0));
                break;
            }
            case 's': {
                size_t slen = 0;
                const char* s = lua_tolstring(L, curArg++, &slen);
                if (s) out.append(s, slen);
                break;
            }
            case 'd':
            case 'i': {
                lua_Integer v = lua_tointeger(L, curArg++);
                out += std::to_string(v);
                break;
            }
            case 'f': {
                double v = lua_tonumber(L, curArg++);
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%f", v);
                out += buf;
                break;
            }
            default:
                out.push_back('%');
                out.push_back(spec);
                break;
        }
    }
    return out;
}

static int Lua_MySQLConnect(lua_State* L) {
    ILuaMySQLComponent* mysql = GetMySQL();
    if (!mysql) {
        lua_pushinteger(L, 0);
        return 1;
    }
    const char* host = luaL_checkstring(L, 1);
    const char* user = luaL_checkstring(L, 2);
    const char* password = luaL_optstring(L, 3, "");
    const char* database = luaL_optstring(L, 4, "");
    uint16_t port = static_cast<uint16_t>(luaL_optinteger(L, 5, 3306));
    bool autoReconnect = lua_isnoneornil(L, 6) ? true : (lua_toboolean(L, 6) != 0);
    int poolSize = static_cast<int>(luaL_optinteger(L, 7, 2));

    int handle = mysql->connect(host, user, password, database, port, autoReconnect, poolSize);
    lua_pushinteger(L, handle);
    return 1;
}

static int Lua_MySQLConnectOOP(lua_State* L) {
    ILuaMySQLComponent* mysql = GetMySQL();
    if (!mysql) {
        lua_pushnil(L);
        return 1;
    }
    const char* host = luaL_checkstring(L, 1);
    const char* user = luaL_checkstring(L, 2);
    const char* password = luaL_optstring(L, 3, "");
    const char* database = luaL_optstring(L, 4, "");
    uint16_t port = static_cast<uint16_t>(luaL_optinteger(L, 5, 3306));
    bool autoReconnect = lua_isnoneornil(L, 6) ? true : (lua_toboolean(L, 6) != 0);
    int poolSize = static_cast<int>(luaL_optinteger(L, 7, 2));

    int handle = mysql->connect(host, user, password, database, port, autoReconnect, poolSize);
    if (handle == 0) {
        lua_pushnil(L);
        return 1;
    }

    LuaMySQLConn* ud = static_cast<LuaMySQLConn*>(lua_newuserdata(L, sizeof(LuaMySQLConn)));
    ud->handle = handle;
    luaL_getmetatable(L, MYSQL_CONN_MT);
    lua_setmetatable(L, -2);
    return 1;
}

static int Lua_MySQLWrapOOP(lua_State* L) {
    ILuaMySQLComponent* mysql = GetMySQL();
    int handle = static_cast<int>(luaL_checkinteger(L, 1));
    if (!mysql || !mysql->isValidHandle(handle)) {
        lua_pushnil(L);
        return 1;
    }
    LuaMySQLConn* ud = static_cast<LuaMySQLConn*>(lua_newuserdata(L, sizeof(LuaMySQLConn)));
    ud->handle = handle;
    luaL_getmetatable(L, MYSQL_CONN_MT);
    lua_setmetatable(L, -2);
    return 1;
}

static int Lua_MySQLClose(lua_State* L) {
    ILuaMySQLComponent* mysql = GetMySQL();
    int handle = GetHandleArg(L, 1);
    lua_pushboolean(L, (mysql && mysql->close(handle)) ? 1 : 0);
    return 1;
}

static int Lua_MySQLErrno(lua_State* L) {
    ILuaMySQLComponent* mysql = GetMySQL();
    int handle = GetHandleArg(L, 1);
    lua_pushinteger(L, mysql ? static_cast<lua_Integer>(mysql->getLastErrno(handle)) : 0);
    return 1;
}

static int Lua_MySQLError(lua_State* L) {
    ILuaMySQLComponent* mysql = GetMySQL();
    int handle = GetHandleArg(L, 1);
    if (!mysql) {
        lua_pushstring(L, "");
        return 1;
    }
    StringView err = mysql->getLastError(handle);
    lua_pushlstring(L, err.data(), err.length());
    return 1;
}

static int Lua_MySQLEscapeString(lua_State* L) {
    ILuaMySQLComponent* mysql = GetMySQL();
    int handle = 0;
    int strIdx = 1;
    if (lua_gettop(L) >= 2) {
        handle = GetHandleArg(L, 1);
        strIdx = 2;
    } else {
        handle = mysql ? mysql->getFirstHandle() : 0;
        strIdx = 1;
    }
    size_t len = 0;
    const char* s = luaL_checklstring(L, strIdx, &len);
    std::string escaped = EscapeViaComponent(mysql, handle, StringView(s, len));
    lua_pushlstring(L, escaped.data(), escaped.size());
    return 1;
}

static int Lua_MySQLFormat(lua_State* L) {
    ILuaMySQLComponent* mysql = GetMySQL();
    int handle = GetHandleArg(L, 1);
    std::string formatted = FormatMySQLString(L, mysql, handle, 2, 3);
    lua_pushlstring(L, formatted.data(), formatted.size());
    return 1;
}

static int Lua_MySQLUnprocessedQueries(lua_State* L) {
    ILuaMySQLComponent* mysql = GetMySQL();
    int handle = GetHandleArg(L, 1);
    lua_pushinteger(L, mysql ? mysql->getUnprocessedQueries(handle) : 0);
    return 1;
}

static int DispatchAsyncQuery(lua_State* L, int handle, int sqlIdx, int cbIdx, int firstArgIdx, bool oopMode) {
    ILuaMySQLComponent* mysql = GetMySQL();
    if (!mysql) {
        lua_pushboolean(L, 0);
        return 1;
    }

    size_t sqlLen = 0;
    const char* sql = luaL_checklstring(L, sqlIdx, &sqlLen);
    Resource* res = GetLuaResource(L);

    int callbackRef = LUA_NOREF;
    std::string callbackName;
    if (!lua_isnoneornil(L, cbIdx)) {
        if (lua_isfunction(L, cbIdx)) {
            lua_pushvalue(L, cbIdx);
            callbackRef = luaL_ref(L, LUA_REGISTRYINDEX);
        } else if (lua_isstring(L, cbIdx)) {
            callbackName = lua_tostring(L, cbIdx);
        } else {
            return luaL_error(L, "MySQL query expected function or function name as callback");
        }
    }

    std::vector<int> argRefs;
    int top = lua_gettop(L);
    for (int i = firstArgIdx; i <= top; ++i) {
        lua_pushvalue(L, i);
        argRefs.push_back(luaL_ref(L, LUA_REGISTRYINDEX));
    }

    LuaMySQLCallback* cb = nullptr;
    if (res && (callbackRef != LUA_NOREF || !callbackName.empty())) {
        cb = new LuaMySQLCallback(res, callbackRef, std::move(callbackName), std::move(argRefs), oopMode);
    } else {
        UnrefRefs(L, callbackRef, argRefs);
    }

    bool queued = mysql->queryAsync(handle, StringView(sql, sqlLen), cb);
    if (!queued && cb) {
        g_PendingCallbacks.erase(cb);
        cb->Cancel(L);
        delete cb;
    }

    lua_pushboolean(L, queued ? 1 : 0);
    return 1;
}

static int Lua_MySQLTQuery(lua_State* L) {
    int handle = GetHandleArg(L, 1);
    return DispatchAsyncQuery(L, handle, 2, 3, 4, false);
}

static int Lua_MySQLConnQueryOOP(lua_State* L) {
    int handle = GetHandleArg(L, 1);
    return DispatchAsyncQuery(L, handle, 2, 3, 4, true);
}

static int Lua_MySQLQuerySync(lua_State* L) {
    ILuaMySQLComponent* mysql = GetMySQL();
    if (!mysql) {
        lua_pushnil(L);
        lua_pushnil(L);
        return 2;
    }

    int handle = GetHandleArg(L, 1);
    size_t sqlLen = 0;
    const char* sql = luaL_checklstring(L, 2, &sqlLen);
    bool useCache = lua_isnoneornil(L, 3) ? true : (lua_toboolean(L, 3) != 0);

    ResultPtr res = WrapResult(mysql->querySync(handle, StringView(sql, sqlLen)), false);
    if (useCache) {
        g_ActiveCache = res;
    }
    PushResultTables(L, res.get());
    return 2;
}

// Cache functions (SA-MP style, 0-based indices)
static int Lua_CacheGetRowCount(lua_State* L) {
    lua_pushinteger(L, g_ActiveCache ? static_cast<lua_Integer>(g_ActiveCache->getRowCount()) : 0);
    return 1;
}

static int Lua_CacheGetFieldCount(lua_State* L) {
    lua_pushinteger(L, g_ActiveCache ? static_cast<lua_Integer>(g_ActiveCache->getFieldCount()) : 0);
    return 1;
}

static int Lua_CacheGetFieldName(lua_State* L) {
    int idx = static_cast<int>(luaL_checkinteger(L, 1));
    if (!g_ActiveCache || idx < 0 || static_cast<size_t>(idx) >= g_ActiveCache->getFieldCount()) {
        lua_pushstring(L, "");
        return 1;
    }
    StringView f = g_ActiveCache->getFieldName(static_cast<size_t>(idx));
    lua_pushlstring(L, f.data(), f.length());
    return 1;
}

static int Lua_CacheGetValueIndex(lua_State* L) {
    int rowIdx = static_cast<int>(luaL_checkinteger(L, 1));
    int fieldIdx = static_cast<int>(luaL_checkinteger(L, 2));
    if (!g_ActiveCache || rowIdx < 0 || fieldIdx < 0 ||
        g_ActiveCache->isNull(static_cast<size_t>(rowIdx), static_cast<size_t>(fieldIdx))) {
        lua_pushstring(L, "");
    } else {
        StringView val = g_ActiveCache->getValue(static_cast<size_t>(rowIdx), static_cast<size_t>(fieldIdx));
        lua_pushlstring(L, val.data(), val.length());
    }
    return 1;
}

static int Lua_CacheGetValueIndexInt(lua_State* L) {
    int rowIdx = static_cast<int>(luaL_checkinteger(L, 1));
    int fieldIdx = static_cast<int>(luaL_checkinteger(L, 2));
    if (!g_ActiveCache || rowIdx < 0 || fieldIdx < 0 ||
        g_ActiveCache->isNull(static_cast<size_t>(rowIdx), static_cast<size_t>(fieldIdx))) {
        lua_pushinteger(L, 0);
    } else {
        StringView val = g_ActiveCache->getValue(static_cast<size_t>(rowIdx), static_cast<size_t>(fieldIdx));
        try {
            lua_pushinteger(L, std::stoll(std::string(val.data(), val.length())));
        } catch (...) {
            lua_pushinteger(L, 0);
        }
    }
    return 1;
}

static int Lua_CacheGetValueIndexFloat(lua_State* L) {
    int rowIdx = static_cast<int>(luaL_checkinteger(L, 1));
    int fieldIdx = static_cast<int>(luaL_checkinteger(L, 2));
    if (!g_ActiveCache || rowIdx < 0 || fieldIdx < 0 ||
        g_ActiveCache->isNull(static_cast<size_t>(rowIdx), static_cast<size_t>(fieldIdx))) {
        lua_pushnumber(L, 0.0);
    } else {
        StringView val = g_ActiveCache->getValue(static_cast<size_t>(rowIdx), static_cast<size_t>(fieldIdx));
        try {
            lua_pushnumber(L, std::stod(std::string(val.data(), val.length())));
        } catch (...) {
            lua_pushnumber(L, 0.0);
        }
    }
    return 1;
}

static int Lua_CacheIsValueIndexNull(lua_State* L) {
    int rowIdx = static_cast<int>(luaL_checkinteger(L, 1));
    int fieldIdx = static_cast<int>(luaL_checkinteger(L, 2));
    bool isNull = !g_ActiveCache || rowIdx < 0 || fieldIdx < 0 ||
                  g_ActiveCache->isNull(static_cast<size_t>(rowIdx), static_cast<size_t>(fieldIdx));
    lua_pushboolean(L, isNull ? 1 : 0);
    return 1;
}

static int Lua_CacheGetValueName(lua_State* L) {
    int rowIdx = static_cast<int>(luaL_checkinteger(L, 1));
    const char* colName = luaL_checkstring(L, 2);
    int fieldIdx = g_ActiveCache ? g_ActiveCache->findField(colName) : -1;
    if (!g_ActiveCache || rowIdx < 0 || fieldIdx < 0 ||
        g_ActiveCache->isNull(static_cast<size_t>(rowIdx), static_cast<size_t>(fieldIdx))) {
        lua_pushstring(L, "");
    } else {
        StringView val = g_ActiveCache->getValue(static_cast<size_t>(rowIdx), static_cast<size_t>(fieldIdx));
        lua_pushlstring(L, val.data(), val.length());
    }
    return 1;
}

static int Lua_CacheGetValueNameInt(lua_State* L) {
    int rowIdx = static_cast<int>(luaL_checkinteger(L, 1));
    const char* colName = luaL_checkstring(L, 2);
    int fieldIdx = g_ActiveCache ? g_ActiveCache->findField(colName) : -1;
    if (!g_ActiveCache || rowIdx < 0 || fieldIdx < 0 ||
        g_ActiveCache->isNull(static_cast<size_t>(rowIdx), static_cast<size_t>(fieldIdx))) {
        lua_pushinteger(L, 0);
    } else {
        StringView val = g_ActiveCache->getValue(static_cast<size_t>(rowIdx), static_cast<size_t>(fieldIdx));
        try {
            lua_pushinteger(L, std::stoll(std::string(val.data(), val.length())));
        } catch (...) {
            lua_pushinteger(L, 0);
        }
    }
    return 1;
}

static int Lua_CacheGetValueNameFloat(lua_State* L) {
    int rowIdx = static_cast<int>(luaL_checkinteger(L, 1));
    const char* colName = luaL_checkstring(L, 2);
    int fieldIdx = g_ActiveCache ? g_ActiveCache->findField(colName) : -1;
    if (!g_ActiveCache || rowIdx < 0 || fieldIdx < 0 ||
        g_ActiveCache->isNull(static_cast<size_t>(rowIdx), static_cast<size_t>(fieldIdx))) {
        lua_pushnumber(L, 0.0);
    } else {
        StringView val = g_ActiveCache->getValue(static_cast<size_t>(rowIdx), static_cast<size_t>(fieldIdx));
        try {
            lua_pushnumber(L, std::stod(std::string(val.data(), val.length())));
        } catch (...) {
            lua_pushnumber(L, 0.0);
        }
    }
    return 1;
}

static int Lua_CacheIsValueNameNull(lua_State* L) {
    int rowIdx = static_cast<int>(luaL_checkinteger(L, 1));
    const char* colName = luaL_checkstring(L, 2);
    int fieldIdx = g_ActiveCache ? g_ActiveCache->findField(colName) : -1;
    bool isNull = !g_ActiveCache || rowIdx < 0 || fieldIdx < 0 ||
                  g_ActiveCache->isNull(static_cast<size_t>(rowIdx), static_cast<size_t>(fieldIdx));
    lua_pushboolean(L, isNull ? 1 : 0);
    return 1;
}

static int Lua_CacheInsertId(lua_State* L) {
    lua_pushinteger(L, g_ActiveCache ? static_cast<lua_Integer>(g_ActiveCache->getInsertId()) : 0);
    return 1;
}

static int Lua_CacheAffectedRows(lua_State* L) {
    lua_pushinteger(L, g_ActiveCache ? static_cast<lua_Integer>(g_ActiveCache->getAffectedRows()) : 0);
    return 1;
}

static int Lua_CacheSave(lua_State* L) {
    if (!g_ActiveCache) {
        lua_pushinteger(L, 0);
        return 1;
    }
    int id = g_NextCacheId++;
    g_SavedCaches.emplace(id, g_ActiveCache);
    lua_pushinteger(L, id);
    return 1;
}

static int Lua_CacheSetActive(lua_State* L) {
    int cacheId = static_cast<int>(luaL_checkinteger(L, 1));
    auto it = g_SavedCaches.find(cacheId);
    if (it == g_SavedCaches.end()) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_ActiveCache = it->second;
    lua_pushboolean(L, 1);
    return 1;
}

static int Lua_CacheDelete(lua_State* L) {
    int cacheId = static_cast<int>(luaL_checkinteger(L, 1));
    lua_pushboolean(L, g_SavedCaches.erase(cacheId) > 0 ? 1 : 0);
    return 1;
}

static int Lua_CacheIsValid(lua_State* L) {
    int cacheId = static_cast<int>(luaL_checkinteger(L, 1));
    lua_pushboolean(L, g_SavedCaches.find(cacheId) != g_SavedCaches.end() ? 1 : 0);
    return 1;
}

static int Lua_MySQLConnHandle(lua_State* L) {
    lua_pushinteger(L, GetHandleArg(L, 1));
    return 1;
}

static int Lua_MySQLConnIsValid(lua_State* L) {
    ILuaMySQLComponent* mysql = GetMySQL();
    int handle = GetHandleArg(L, 1);
    lua_pushboolean(L, (mysql && mysql->isValidHandle(handle)) ? 1 : 0);
    return 1;
}

} // namespace

void CleanupMySQLForResource(Resource* resource) {
    if (!resource) return;
    lua_State* L = resource->GetLua();
    for (LuaMySQLCallback* cb : g_PendingCallbacks) {
        if (cb && cb->GetResource() == resource) {
            cb->Cancel(L);
        }
    }
}

void RegisterMySQLBindings(lua_State* L, Resource* /*resource*/) {
    if (!GetMySQL()) {
        return;
    }

    if (luaL_newmetatable(L, MYSQL_CONN_MT)) {
        lua_createtable(L, 0, 11);

        lua_pushcfunction(L, Lua_MySQLConnQueryOOP);
        lua_setfield(L, -2, "query");

        lua_pushcfunction(L, Lua_MySQLQuerySync);
        lua_setfield(L, -2, "querySync");

        lua_pushcfunction(L, Lua_MySQLQuerySync);
        lua_setfield(L, -2, "query_sync");

        lua_pushcfunction(L, Lua_MySQLEscapeString);
        lua_setfield(L, -2, "escape");

        lua_pushcfunction(L, Lua_MySQLFormat);
        lua_setfield(L, -2, "format");

        lua_pushcfunction(L, Lua_MySQLClose);
        lua_setfield(L, -2, "close");

        lua_pushcfunction(L, Lua_MySQLErrno);
        lua_setfield(L, -2, "errno");

        lua_pushcfunction(L, Lua_MySQLError);
        lua_setfield(L, -2, "error");

        lua_pushcfunction(L, Lua_MySQLUnprocessedQueries);
        lua_setfield(L, -2, "unprocessed");

        lua_pushcfunction(L, Lua_MySQLConnHandle);
        lua_setfield(L, -2, "handle");

        lua_pushcfunction(L, Lua_MySQLConnIsValid);
        lua_setfield(L, -2, "isValid");

        lua_setfield(L, -2, "__index");
    }
    lua_pop(L, 1);

    lua_createtable(L, 0, 4);
    lua_pushcfunction(L, Lua_MySQLConnectOOP);
    lua_setfield(L, -2, "Connect");
    lua_pushcfunction(L, Lua_MySQLConnectOOP);
    lua_setfield(L, -2, "connect");
    lua_pushcfunction(L, Lua_MySQLWrapOOP);
    lua_setfield(L, -2, "Wrap");
    lua_pushcfunction(L, Lua_MySQLEscapeString);
    lua_setfield(L, -2, "Escape");
    lua_setglobal(L, "MySQL");

    lua_register(L, "mysql_connect", Lua_MySQLConnect);
    lua_register(L, "mysql_close", Lua_MySQLClose);
    lua_register(L, "mysql_errno", Lua_MySQLErrno);
    lua_register(L, "mysql_error", Lua_MySQLError);
    lua_register(L, "mysql_escape_string", Lua_MySQLEscapeString);
    lua_register(L, "mysql_format", Lua_MySQLFormat);
    lua_register(L, "mysql_tquery", Lua_MySQLTQuery);
    lua_register(L, "mysql_pquery", Lua_MySQLTQuery);
    lua_register(L, "mysql_query", Lua_MySQLQuerySync);
    lua_register(L, "mysql_unprocessed_queries", Lua_MySQLUnprocessedQueries);

    lua_register(L, "cache_get_row_count", Lua_CacheGetRowCount);
    lua_register(L, "cache_get_field_count", Lua_CacheGetFieldCount);
    lua_register(L, "cache_get_field_name", Lua_CacheGetFieldName);
    lua_register(L, "cache_get_value_index", Lua_CacheGetValueIndex);
    lua_register(L, "cache_get_value_index_int", Lua_CacheGetValueIndexInt);
    lua_register(L, "cache_get_value_index_float", Lua_CacheGetValueIndexFloat);
    lua_register(L, "cache_is_value_index_null", Lua_CacheIsValueIndexNull);
    lua_register(L, "cache_get_value_name", Lua_CacheGetValueName);
    lua_register(L, "cache_get_value_name_int", Lua_CacheGetValueNameInt);
    lua_register(L, "cache_get_value_name_float", Lua_CacheGetValueNameFloat);
    lua_register(L, "cache_is_value_name_null", Lua_CacheIsValueNameNull);
    lua_register(L, "cache_insert_id", Lua_CacheInsertId);
    lua_register(L, "cache_affected_rows", Lua_CacheAffectedRows);
    lua_register(L, "cache_save", Lua_CacheSave);
    lua_register(L, "cache_set_active", Lua_CacheSetActive);
    lua_register(L, "cache_delete", Lua_CacheDelete);
    lua_register(L, "cache_is_valid", Lua_CacheIsValid);

    lua_pushinteger(L, 0);
    lua_setglobal(L, "MYSQL_INVALID_HANDLE");
    lua_pushinteger(L, 0);
    lua_setglobal(L, "MYSQL_INVALID_CACHE");
}

} // namespace Resa
