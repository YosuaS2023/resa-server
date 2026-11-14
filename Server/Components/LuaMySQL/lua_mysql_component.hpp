/*
 *  resa-multiplayer (resam)
 *  LuaMySQL Component Header
 */

#pragma once

#include <Server/Components/LuaMySQL/lua_mysql.hpp>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Resa {

struct ConnectionPool;

class LuaMySQLComponent final : public ILuaMySQLComponent, public CoreEventHandler {
public:
    LuaMySQLComponent() = default;
    ~LuaMySQLComponent() override = default;

    StringView componentName() const override {
        return "LuaMySQL";
    }

    SemanticVersion componentVersion() const override {
        return SemanticVersion(1, 0, 0, 0);
    }

    void onLoad(ICore* c) override;
    void onInit(IComponentList* components) override {}
    void onReady() override;
    void onFree(IComponent* component) override {}
    void free() override;
    void reset() override {}

    void onTick(Microseconds elapsed, TimePoint now) override;

    int connect(StringView host, StringView user, StringView password,
                StringView database, uint16_t port = 3306,
                bool autoReconnect = true, int poolSize = 2) override;
    bool close(int handle) override;
    bool isValidHandle(int handle) const override;
    unsigned int getLastErrno(int handle) const override;
    StringView getLastError(int handle) const override;
    int getFirstHandle() const override;
    int getUnprocessedQueries(int handle) const override;

    void escapeString(int handle, StringView input,
                      void (*sink)(void* ctx, const char* data, size_t len),
                      void* ctx) override;

    IMySQLResult* querySync(int handle, StringView sql) override;
    bool queryAsync(int handle, StringView sql, IMySQLQueryCallback* callback) override;

private:
    struct CompletedTask {
        IMySQLQueryCallback* callback = nullptr;
        IMySQLResult* result = nullptr;
    };

    ICore* core_ = nullptr;
    bool initialized_ = false;

    mutable std::mutex connMutex_;
    int nextHandle_ = 1;
    std::unordered_map<int, std::shared_ptr<ConnectionPool>> connections_;

    std::mutex completedMutex_;
    std::vector<CompletedTask> completedQueue_;

    // Thread-local/last-error buffer for StringView return in getLastError
    mutable std::mutex errReturnMutex_;
    mutable std::string lastErrorCache_;
};

} // namespace Resa
