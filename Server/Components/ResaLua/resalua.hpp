/*
 *  resa-multiplayer (resam)
 *  Modular Resource-Based Lua Scripting Engine
 *
 *  Copyright (c) 2026 resa-multiplayer contributors
 */

#pragma once

#include <sdk.hpp>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

namespace Resa {

static constexpr UID ResaLua_UID = UID(0x524553414C554101ULL); // "RESALUA\x01"

enum class ResourceState {
    Stopped,
    Starting,
    Running,
    Stopping
};

struct ResourceMetaData {
    std::string name;
    std::string path;
    std::string version = "1.0.0";
    std::string author = "Unknown";
    std::string description;
    std::vector<std::string> serverScripts;
    std::vector<std::string> sharedScripts;
    std::vector<std::string> dependencies;
    std::vector<std::string> exports;
};

class ResourceManager;
class Resource;

} // namespace Resa
