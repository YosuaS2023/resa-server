/*
 *  resa-multiplayer (resam)
 *  LuaMySQL Component Implementation
 */

#include "lua_mysql_component.hpp"

#include <mysql.h>
#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <cstring>
#include <queue>
#include <thread>

namespace Resa {

namespace {

std::string ToLowerStr(StringView sv) {
    std::string out(sv.data(), sv.length());
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

std::string FallbackEscape(StringView input) {
    std::string out;
    out.reserve(input.length() * 2);
    for (size_t i = 0; i < input.length(); ++i) {
        unsigned char c = static_cast<unsigned char>(input[i]);
        switch (c) {
            case '\0': out += "\\0"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\\': out += "\\\\"; break;
            case '\'': out += "\\'"; break;
            case '"':  out += "\\\""; break;
            case '\x1a': out += "\\Z"; break;
            default: out.push_back(static_cast<char>(c)); break;
        }
    }
    return out;
}

struct MySQLCell {
    bool isNull = true;
    std::string value;
};

class MySQLResultImpl final : public IMySQLResult {
public:
    void retain() override {
        refCount_.fetch_add(1, std::memory_order_relaxed);
    }

    void release() override {
        if (refCount_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete this;
        }
    }

    bool isOk() const override { return ok; }
    unsigned int getErrno() const override { return errNo; }
    StringView getError() const override { return StringView(error.data(), error.size()); }
    uint64_t getInsertId() const override { return insertId; }
    uint64_t getAffectedRows() const override { return affectedRows; }

    size_t getRowCount() const override { return rows.size(); }
    size_t getFieldCount() const override { return fields.size(); }

    StringView getFieldName(size_t fieldIdx) const override {
        if (fieldIdx >= fields.size()) return StringView();
        return StringView(fields[fieldIdx].data(), fields[fieldIdx].size());
    }

    int findField(StringView name) const override {
        std::string key(name.data(), name.length());
        auto it = fieldIndex.find(key);
        if (it != fieldIndex.end()) {
            return static_cast<int>(it->second);
        }
        auto lowerIt = fieldIndex.find(ToLowerStr(name));
        if (lowerIt != fieldIndex.end()) {
            return static_cast<int>(lowerIt->second);
        }
        return -1;
    }

    bool isNull(size_t rowIdx, size_t fieldIdx) const override {
        if (rowIdx >= rows.size() || fieldIdx >= rows[rowIdx].size()) return true;
        return rows[rowIdx][fieldIdx].isNull;
    }

    StringView getValue(size_t rowIdx, size_t fieldIdx) const override {
        if (rowIdx >= rows.size() || fieldIdx >= rows[rowIdx].size()) return StringView();
        const auto& cell = rows[rowIdx][fieldIdx];
        if (cell.isNull) return StringView();
        return StringView(cell.value.data(), cell.value.size());
    }

    bool ok = false;
    unsigned int errNo = 0;
    std::string error;
    uint64_t insertId = 0;
    uint64_t affectedRows = 0;
    std::vector<std::string> fields;
    std::unordered_map<std::string, size_t> fieldIndex;
    std::vector<std::vector<MySQLCell>> rows;

private:
    std::atomic<int> refCount_{1};
};

MYSQL* CreateRawConnection(const std::string& host, const std::string& user,
                           const std::string& password, const std::string& database,
                           uint16_t port, bool autoReconnect,
                           unsigned int& outErrno, std::string& outError) {
    MYSQL* conn = mysql_init(nullptr);
    if (!conn) {
        outErrno = 2008;
        outError = "mysql_init() failed (out of memory)";
        return nullptr;
    }

    if (autoReconnect) {
        bool reconnect = true;
        mysql_options(conn, MYSQL_OPT_RECONNECT, &reconnect);
    }

    unsigned int connectTimeout = 10;
    mysql_options(conn, MYSQL_OPT_CONNECT_TIMEOUT, &connectTimeout);
    mysql_options(conn, MYSQL_SET_CHARSET_NAME, "utf8mb4");

    if (!mysql_real_connect(conn, host.c_str(), user.c_str(), password.c_str(),
                            database.empty() ? nullptr : database.c_str(),
                            port, nullptr, 0)) {
        outErrno = mysql_errno(conn);
        const char* err = mysql_error(conn);
        outError = err ? err : "Unknown MySQL connection error";
        mysql_close(conn);
        return nullptr;
    }

    outErrno = 0;
    outError.clear();
    return conn;
}

MySQLResultImpl* ExecuteOnRawConnection(MYSQL* conn, const std::string& sql,
                                        const std::string& host,
                                        const std::string& user,
                                        const std::string& password,
                                        const std::string& database,
                                        uint16_t port, bool autoReconnect) {
    auto* res = new MySQLResultImpl();
    if (!conn) {
        res->ok = false;
        res->errNo = 2006;
        res->error = "MySQL connection is not initialized";
        return res;
    }

    if (autoReconnect && mysql_ping(conn) != 0) {
        mysql_real_connect(conn, host.c_str(), user.c_str(), password.c_str(),
                           database.empty() ? nullptr : database.c_str(),
                           port, nullptr, 0);
    }

    if (mysql_real_query(conn, sql.data(), static_cast<unsigned long>(sql.size())) != 0) {
        res->ok = false;
        res->errNo = mysql_errno(conn);
        const char* err = mysql_error(conn);
        res->error = err ? err : "Unknown MySQL query error";
        return res;
    }

    res->ok = true;
    res->insertId = static_cast<uint64_t>(mysql_insert_id(conn));
    uint64_t affected = static_cast<uint64_t>(mysql_affected_rows(conn));
    res->affectedRows = (affected == static_cast<uint64_t>(-1)) ? 0 : affected;

    MYSQL_RES* mysqlRes = mysql_store_result(conn);
    if (mysqlRes) {
        unsigned int numFields = mysql_num_fields(mysqlRes);
        MYSQL_FIELD* fields = mysql_fetch_fields(mysqlRes);
        res->fields.reserve(numFields);
        for (unsigned int i = 0; i < numFields; ++i) {
            std::string name = fields[i].name ? fields[i].name : "";
            res->fieldIndex.emplace(name, i);
            std::string lower = ToLowerStr(StringView(name.data(), name.size()));
            if (res->fieldIndex.find(lower) == res->fieldIndex.end()) {
                res->fieldIndex.emplace(std::move(lower), i);
            }
            res->fields.push_back(std::move(name));
        }

        MYSQL_ROW row;
        while ((row = mysql_fetch_row(mysqlRes)) != nullptr) {
            unsigned long* lengths = mysql_fetch_lengths(mysqlRes);
            std::vector<MySQLCell> rowCells(numFields);
            for (unsigned int i = 0; i < numFields; ++i) {
                if (row[i] == nullptr) {
                    rowCells[i].isNull = true;
                } else {
                    rowCells[i].isNull = false;
                    rowCells[i].value.assign(row[i], lengths ? lengths[i] : std::strlen(row[i]));
                }
            }
            res->rows.push_back(std::move(rowCells));
        }
        mysql_free_result(mysqlRes);
    } else if (mysql_field_count(conn) != 0) {
        res->ok = false;
        res->errNo = mysql_errno(conn);
        const char* err = mysql_error(conn);
        res->error = err ? err : "Failed to store MySQL result";
    }

    while (mysql_next_result(conn) == 0) {
        MYSQL_RES* extra = mysql_store_result(conn);
        if (extra) {
            mysql_free_result(extra);
        }
    }

    return res;
}

struct AsyncQueryTask {
    std::string sql;
    IMySQLQueryCallback* callback = nullptr;
};

} // namespace

struct ConnectionPool {
    int handle = 0;
    std::string host;
    std::string user;
    std::string password;
    std::string database;
    uint16_t port = 3306;
    bool autoReconnect = true;

    std::mutex syncMutex;
    MYSQL* syncConn = nullptr;

    mutable std::mutex errMutex;
    unsigned int lastErrno = 0;
    std::string lastError;

    std::mutex queueMutex;
    std::condition_variable queueCv;
    std::queue<AsyncQueryTask> tasks;
    std::atomic<int> pendingCount{0};
    bool stopping = false;
    std::vector<std::thread> workers;

    void SetError(unsigned int errNo, const std::string& err) {
        std::lock_guard<std::mutex> lock(errMutex);
        lastErrno = errNo;
        lastError = err;
    }

    void StopWorkers() {
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            stopping = true;
        }
        queueCv.notify_all();
        for (auto& t : workers) {
            if (t.joinable()) {
                t.join();
            }
        }
        workers.clear();

        std::lock_guard<std::mutex> slock(syncMutex);
        if (syncConn) {
            mysql_close(syncConn);
            syncConn = nullptr;
        }
    }
};

void LuaMySQLComponent::onLoad(ICore* c) {
    core_ = c;
    if (!initialized_) {
        mysql_library_init(0, nullptr, nullptr);
        initialized_ = true;
    }
}

void LuaMySQLComponent::onReady() {
    if (core_) {
        core_->getEventDispatcher().addEventHandler(this);
    }
}

void LuaMySQLComponent::free() {
    if (core_) {
        core_->getEventDispatcher().removeEventHandler(this);
    }

    std::unordered_map<int, std::shared_ptr<ConnectionPool>> toClose;
    {
        std::lock_guard<std::mutex> lock(connMutex_);
        toClose.swap(connections_);
    }
    for (auto& kv : toClose) {
        kv.second->StopWorkers();
    }

    {
        std::lock_guard<std::mutex> lock(completedMutex_);
        for (auto& task : completedQueue_) {
            if (task.result) {
                task.result->release();
            }
        }
        completedQueue_.clear();
    }

    if (initialized_) {
        mysql_library_end();
        initialized_ = false;
    }
    delete this;
}

void LuaMySQLComponent::onTick(Microseconds /*elapsed*/, TimePoint /*now*/) {
    std::vector<CompletedTask> batch;
    {
        std::lock_guard<std::mutex> lock(completedMutex_);
        if (completedQueue_.empty()) return;
        batch.swap(completedQueue_);
    }

    for (auto& task : batch) {
        if (task.callback) {
            task.callback->onQueryComplete(task.result);
        }
        if (task.result) {
            task.result->release();
        }
    }
}

int LuaMySQLComponent::connect(StringView host, StringView user, StringView password,
                               StringView database, uint16_t port,
                               bool autoReconnect, int poolSize) {
    if (!initialized_) {
        mysql_library_init(0, nullptr, nullptr);
        initialized_ = true;
    }
    if (poolSize < 1) poolSize = 1;
    if (poolSize > 16) poolSize = 16;

    std::string hostStr(host.data(), host.length());
    std::string userStr(user.data(), user.length());
    std::string passStr(password.data(), password.length());
    std::string dbStr(database.data(), database.length());

    unsigned int errNo = 0;
    std::string errStr;
    MYSQL* syncConn = CreateRawConnection(hostStr, userStr, passStr, dbStr, port, autoReconnect, errNo, errStr);
    if (!syncConn) {
        if (core_) {
            core_->logLn(LogLevel::Error, "[LuaMySQL] Connection failed (%u): %s", errNo, errStr.c_str());
        }
        return 0;
    }

    auto pool = std::make_shared<ConnectionPool>();
    pool->host = std::move(hostStr);
    pool->user = std::move(userStr);
    pool->password = std::move(passStr);
    pool->database = std::move(dbStr);
    pool->port = port;
    pool->autoReconnect = autoReconnect;
    pool->syncConn = syncConn;

    int handle = 0;
    {
        std::lock_guard<std::mutex> lock(connMutex_);
        handle = nextHandle_++;
        pool->handle = handle;
        connections_.emplace(handle, pool);
    }

    for (int i = 0; i < poolSize; ++i) {
        pool->workers.emplace_back([this, pool]() {
            mysql_thread_init();
            unsigned int wErr = 0;
            std::string wErrStr;
            MYSQL* workerConn = CreateRawConnection(pool->host, pool->user, pool->password,
                                                    pool->database, pool->port, pool->autoReconnect,
                                                    wErr, wErrStr);

            while (true) {
                AsyncQueryTask task;
                {
                    std::unique_lock<std::mutex> lock(pool->queueMutex);
                    pool->queueCv.wait(lock, [&pool]() {
                        return pool->stopping || !pool->tasks.empty();
                    });
                    if (pool->stopping && pool->tasks.empty()) {
                        break;
                    }
                    task = std::move(pool->tasks.front());
                    pool->tasks.pop();
                }

                if (!workerConn) {
                    workerConn = CreateRawConnection(pool->host, pool->user, pool->password,
                                                     pool->database, pool->port, pool->autoReconnect,
                                                     wErr, wErrStr);
                }

                MySQLResultImpl* result = nullptr;
                if (!workerConn) {
                    result = new MySQLResultImpl();
                    result->ok = false;
                    result->errNo = wErr;
                    result->error = wErrStr;
                } else {
                    result = ExecuteOnRawConnection(workerConn, task.sql,
                                                    pool->host, pool->user, pool->password,
                                                    pool->database, pool->port, pool->autoReconnect);
                }

                if (!result->ok) {
                    pool->SetError(result->errNo, result->error);
                } else {
                    pool->SetError(0, "");
                }

                pool->pendingCount.fetch_sub(1, std::memory_order_relaxed);

                if (task.callback) {
                    CompletedTask done;
                    done.callback = task.callback;
                    done.result = result;
                    std::lock_guard<std::mutex> clock(completedMutex_);
                    completedQueue_.push_back(done);
                } else {
                    result->release();
                }
            }

            if (workerConn) {
                mysql_close(workerConn);
            }
            mysql_thread_end();
        });
    }

    return handle;
}

bool LuaMySQLComponent::close(int handle) {
    std::shared_ptr<ConnectionPool> pool;
    {
        std::lock_guard<std::mutex> lock(connMutex_);
        auto it = connections_.find(handle);
        if (it == connections_.end()) {
            return false;
        }
        pool = std::move(it->second);
        connections_.erase(it);
    }
    pool->StopWorkers();
    return true;
}

bool LuaMySQLComponent::isValidHandle(int handle) const {
    std::lock_guard<std::mutex> lock(connMutex_);
    return connections_.find(handle) != connections_.end();
}

unsigned int LuaMySQLComponent::getLastErrno(int handle) const {
    std::shared_ptr<ConnectionPool> pool;
    {
        std::lock_guard<std::mutex> lock(connMutex_);
        auto it = connections_.find(handle);
        if (it == connections_.end()) return 0;
        pool = it->second;
    }
    std::lock_guard<std::mutex> elock(pool->errMutex);
    return pool->lastErrno;
}

StringView LuaMySQLComponent::getLastError(int handle) const {
    std::shared_ptr<ConnectionPool> pool;
    {
        std::lock_guard<std::mutex> lock(connMutex_);
        auto it = connections_.find(handle);
        if (it == connections_.end()) return StringView();
        pool = it->second;
    }
    std::lock_guard<std::mutex> elock(pool->errMutex);
    std::lock_guard<std::mutex> rlock(errReturnMutex_);
    lastErrorCache_ = pool->lastError;
    return StringView(lastErrorCache_.data(), lastErrorCache_.size());
}

int LuaMySQLComponent::getFirstHandle() const {
    std::lock_guard<std::mutex> lock(connMutex_);
    if (connections_.empty()) return 0;
    return connections_.begin()->first;
}

int LuaMySQLComponent::getUnprocessedQueries(int handle) const {
    std::lock_guard<std::mutex> lock(connMutex_);
    auto it = connections_.find(handle);
    if (it == connections_.end()) return 0;
    return it->second->pendingCount.load(std::memory_order_relaxed);
}

void LuaMySQLComponent::escapeString(int handle, StringView input,
                                     void (*sink)(void* ctx, const char* data, size_t len),
                                     void* ctx) {
    if (!sink) return;
    std::shared_ptr<ConnectionPool> pool;
    {
        std::lock_guard<std::mutex> lock(connMutex_);
        auto it = connections_.find(handle);
        if (it != connections_.end()) {
            pool = it->second;
        }
    }

    if (!pool) {
        std::string esc = FallbackEscape(input);
        sink(ctx, esc.data(), esc.size());
        return;
    }

    std::lock_guard<std::mutex> slock(pool->syncMutex);
    if (!pool->syncConn) {
        std::string esc = FallbackEscape(input);
        sink(ctx, esc.data(), esc.size());
        return;
    }

    std::string out;
    out.resize(input.length() * 2 + 1);
    unsigned long len = mysql_real_escape_string(
        pool->syncConn, &out[0], input.data(), static_cast<unsigned long>(input.length()));
    sink(ctx, out.data(), static_cast<size_t>(len));
}

IMySQLResult* LuaMySQLComponent::querySync(int handle, StringView sql) {
    std::shared_ptr<ConnectionPool> pool;
    {
        std::lock_guard<std::mutex> lock(connMutex_);
        auto it = connections_.find(handle);
        if (it == connections_.end()) {
            auto* errRes = new MySQLResultImpl();
            errRes->ok = false;
            errRes->errNo = 2006;
            errRes->error = "Invalid MySQL handle";
            return errRes;
        }
        pool = it->second;
    }

    std::string sqlStr(sql.data(), sql.length());
    MySQLResultImpl* result = nullptr;
    {
        std::lock_guard<std::mutex> slock(pool->syncMutex);
        result = ExecuteOnRawConnection(pool->syncConn, sqlStr,
                                        pool->host, pool->user, pool->password,
                                        pool->database, pool->port, pool->autoReconnect);
    }

    if (!result->ok) {
        pool->SetError(result->errNo, result->error);
    } else {
        pool->SetError(0, "");
    }
    return result;
}

bool LuaMySQLComponent::queryAsync(int handle, StringView sql, IMySQLQueryCallback* callback) {
    std::shared_ptr<ConnectionPool> pool;
    {
        std::lock_guard<std::mutex> lock(connMutex_);
        auto it = connections_.find(handle);
        if (it == connections_.end()) {
            return false;
        }
        pool = it->second;
    }

    AsyncQueryTask task;
    task.sql.assign(sql.data(), sql.length());
    task.callback = callback;

    pool->pendingCount.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> qlock(pool->queueMutex);
        pool->tasks.push(std::move(task));
    }
    pool->queueCv.notify_one();
    return true;
}

} // namespace Resa
