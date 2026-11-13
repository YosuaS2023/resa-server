/*
 *  resa-multiplayer (resam)
 *  Resource Definition
 */

#pragma once

#include "lua_defs.hpp"

namespace Resa {

class Resource {
public:
    Resource(const std::string& name, const std::string& path, ResourceManager* manager);
    ~Resource();

    bool LoadManifest();
    bool Start();
    bool Stop();
    bool Restart();

    const std::string& GetName() const { return meta_.name; }
    const std::string& GetPath() const { return meta_.path; }
    ResourceState GetState() const { return state_; }
    const ResourceMetaData& GetMeta() const { return meta_; }
    lua_State* GetLua() const { return L_; }

private:
    void InitLuaEnvironment();
    void CleanupLuaEnvironment();
    bool ExecuteScript(const std::string& relativePath);

    ResourceMetaData meta_;
    ResourceState state_ = ResourceState::Stopped;
    ResourceManager* manager_ = nullptr;
    lua_State* L_ = nullptr;
};

} // namespace Resa
