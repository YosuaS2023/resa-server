/*
 *  resa-multiplayer (resam)
 *  Resource Manager
 */

#pragma once

#include "resalua.hpp"
#include <unordered_map>
#include <memory>
#include <string>
#include <vector>

namespace Resa {

class ResourceManager {
public:
    static ResourceManager& Get();

    void Init(ICore* core, IComponentList* components);
    void Shutdown();

    // Resource discovery
    void Refresh();

    // Lifecycle
    bool StartResource(const std::string& name);
    bool StopResource(const std::string& name);
    bool RestartResource(const std::string& name);
    bool EnsureResource(const std::string& name);

    Resource* GetResource(const std::string& name);
    const std::unordered_map<std::string, std::shared_ptr<Resource>>& GetResources() const { return resources_; }

    ICore* GetCore() const { return core_; }
    IComponentList* GetComponents() const { return components_; }

    // Log helper
    void Log(const std::string& message);
    void LogError(const std::string& message);

    // Cross-resource export support
    void RegisterExport(const std::string& resourceName, const std::string& exportName, int luaRef);
    int GetExportRef(const std::string& resourceName, const std::string& exportName);

private:
    ResourceManager() = default;

    void ScanDirectory(const std::string& path);

    ICore* core_ = nullptr;
    IComponentList* components_ = nullptr;
    std::unordered_map<std::string, std::shared_ptr<Resource>> resources_;
    // exports[resourceName][exportName] = luaRef
    std::unordered_map<std::string, std::unordered_map<std::string, int>> exports_;
};

} // namespace Resa
