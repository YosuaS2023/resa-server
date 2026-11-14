/*
 *  resa-multiplayer (resam)
 *  LuaMySQL Component Interface
 */

#pragma once

#include <sdk.hpp>
#include <cstddef>
#include <cstdint>

namespace Resa {

static constexpr UID LuaMySQL_UID = UID(0x4C55414D5953514CULL); // "LUAMYSQL"

struct IMySQLResult {
    virtual ~IMySQLResult() = default;
    virtual void retain() = 0;
    virtual void release() = 0;

    virtual bool isOk() const = 0;
    virtual unsigned int getErrno() const = 0;
    virtual StringView getError() const = 0;
    virtual uint64_t getInsertId() const = 0;
    virtual uint64_t getAffectedRows() const = 0;

    virtual size_t getRowCount() const = 0;
    virtual size_t getFieldCount() const = 0;
    virtual StringView getFieldName(size_t fieldIdx) const = 0;
    virtual int findField(StringView name) const = 0;
    virtual bool isNull(size_t rowIdx, size_t fieldIdx) const = 0;
    virtual StringView getValue(size_t rowIdx, size_t fieldIdx) const = 0;
};

struct IMySQLQueryCallback {
    virtual ~IMySQLQueryCallback() = default;
    virtual void onQueryComplete(IMySQLResult* result) = 0;
};

struct ILuaMySQLComponent : public IComponent {
    PROVIDE_UID(LuaMySQL_UID);

    virtual int connect(StringView host, StringView user, StringView password,
                        StringView database, uint16_t port = 3306,
                        bool autoReconnect = true, int poolSize = 2) = 0;
    virtual bool close(int handle) = 0;
    virtual bool isValidHandle(int handle) const = 0;
    virtual unsigned int getLastErrno(int handle) const = 0;
    virtual StringView getLastError(int handle) const = 0;
    virtual int getFirstHandle() const = 0;
    virtual int getUnprocessedQueries(int handle) const = 0;

    virtual void escapeString(int handle, StringView input,
                              void (*sink)(void* ctx, const char* data, size_t len),
                              void* ctx) = 0;

    virtual IMySQLResult* querySync(int handle, StringView sql) = 0;
    virtual bool queryAsync(int handle, StringView sql, IMySQLQueryCallback* callback) = 0;
};

} // namespace Resa
