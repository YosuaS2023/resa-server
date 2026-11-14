/*
 *  resa-multiplayer (resam)
 *  Resource Manager Implementation
 */

#include "resource_manager.hpp"
#include "resource.hpp"
#include <ghc/filesystem.hpp>
#include <iostream>

namespace Resa {

namespace fs = ghc::filesystem;

ResourceManager& ResourceManager::Get() {
    static ResourceManager instance;
    return instance;
}

void ResourceManager::Init(ICore* core, IComponentList* components) {
    core_ = core;
    components_ = components;

    Log("Initializing Resa Lua Resource Manager...");

    // Create resources directory if it doesn't exist
    if (!fs::exists("resources")) {
        fs::create_directories("resources");
    }

    Refresh();
}

void ResourceManager::Shutdown() {
    Log("Shutting down Resa Resource Manager...");
    for (auto& pair : resources_) {
        if (pair.second->GetState() == ResourceState::Running) {
            pair.second->Stop();
        }
    }
    resources_.clear();
}

void ResourceManager::ScanDirectory(const std::string& pathStr) {
    if (!fs::exists(pathStr)) return;

    for (const auto& entry : fs::directory_iterator(pathStr)) {
        if (entry.is_directory()) {
            std::string folderName = entry.path().filename().string();

            // FiveM category folder pattern: [folderName]
            if (folderName.front() == '[' && folderName.back() == ']') {
                ScanDirectory(entry.path().string());
            } else {
                // Check if this folder is a resource
                bool hasManifest = fs::exists(entry.path() / "fxmanifest.lua") || fs::exists(entry.path() / "resmanifest.lua");
                if (hasManifest) {
                    std::string resName = folderName;
                    auto it = resources_.find(resName);
                    if (it == resources_.end()) {
                        auto res = std::make_shared<Resource>(resName, entry.path().string(), this);
                        if (res->LoadManifest()) {
                            resources_[resName] = res;
                            Log("Discovered resource: " + resName + " (" + entry.path().string() + ")");
                        }
                    } else {
                        it->second->LoadManifest();
                    }
                }
            }
        }
    }
}

void ResourceManager::Refresh() {
    Log("Scanning 'resources/' directory for modular resources...");
    ScanDirectory("resources");
    Log("Total resources loaded: " + std::to_string(resources_.size()));
}

Resource* ResourceManager::GetResource(const std::string& name) {
    auto it = resources_.find(name);
    return it != resources_.end() ? it->second.get() : nullptr;
}

bool ResourceManager::StartResource(const std::string& name) {
    Resource* res = GetResource(name);
    if (!res) {
        // Try refresh once
        Refresh();
        res = GetResource(name);
        if (!res) {
            LogError("Cannot start resource '" + name + "': Resource not found.");
            return false;
        }
    }

    // Resolve dependencies
    for (const auto& dep : res->GetMeta().dependencies) {
        Resource* depRes = GetResource(dep);
        if (!depRes || depRes->GetState() != ResourceState::Running) {
            Log("Starting dependency '" + dep + "' for '" + name + "'...");
            if (!StartResource(dep)) {
                LogError("Failed to start dependency '" + dep + "' for '" + name + "'");
                return false;
            }
        }
    }

    return res->Start();
}

bool ResourceManager::StopResource(const std::string& name) {
    Resource* res = GetResource(name);
    if (!res) {
        LogError("Cannot stop resource '" + name + "': Resource not found.");
        return false;
    }
    return res->Stop();
}

bool ResourceManager::RestartResource(const std::string& name) {
    Resource* res = GetResource(name);
    if (!res) {
        LogError("Cannot restart resource '" + name + "': Resource not found.");
        return false;
    }
    return res->Restart();
}

bool ResourceManager::EnsureResource(const std::string& name) {
    Resource* res = GetResource(name);
    if (res && res->GetState() == ResourceState::Running) {
        return true;
    }
    return StartResource(name);
}

void ResourceManager::Log(const std::string& message) {
    if (core_) {
        core_->logLn(LogLevel::Message, "[Resa-Lua] %s", message.c_str());
    } else {
        std::cout << "[Resa-Lua] " << message << std::endl;
    }
}

void ResourceManager::LogError(const std::string& message) {
    if (core_) {
        core_->logLn(LogLevel::Error, "[Resa-Lua] %s", message.c_str());
    } else {
        std::cerr << "[Resa-Lua][ERROR] " << message << std::endl;
    }
}

void ResourceManager::RegisterExport(const std::string& resourceName, const std::string& exportName, int luaRef) {
    exports_[resourceName][exportName] = luaRef;
}

int ResourceManager::GetExportRef(const std::string& resourceName, const std::string& exportName) {
    auto rIt = exports_.find(resourceName);
    if (rIt != exports_.end()) {
        auto eIt = rIt->second.find(exportName);
        if (eIt != rIt->second.end()) {
            return eIt->second;
        }
    }
    return LUA_NOREF;
}

void ResourceManager::RemoveExportsForResource(const std::string& resourceName) {
    exports_.erase(resourceName);
}

} // namespace Resa
