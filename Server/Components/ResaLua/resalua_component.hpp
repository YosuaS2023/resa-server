/*
 *  resa-multiplayer (resam)
 *  Component Header
 */

#pragma once

#include "resalua.hpp"
#include "resource.hpp"
#include "resource_manager.hpp"
#include "event_manager.hpp"
#include <Server/Components/Console/console.hpp>

namespace Resa {

class ResaLuaComponent final : public IComponent,
                               public ConsoleEventHandler,
                               public PlayerConnectEventHandler,
                               public PlayerSpawnEventHandler,
                               public PlayerTextEventHandler {
public:
    PROVIDE_UID(ResaLua_UID)

    ResaLuaComponent() = default;
    ~ResaLuaComponent() override = default;

    StringView componentName() const override {
        return "ResaLua";
    }

    SemanticVersion componentVersion() const override {
        return SemanticVersion(1, 0, 0, 0);
    }

    void onLoad(ICore* c) override {
        core_ = c;
    }

    void onInit(IComponentList* components) override {
        components_ = components;
        console_ = components->queryComponent<IConsoleComponent>();
        if (console_) {
            console_->getEventDispatcher().addEventHandler(this);
        }
    }

    void onReady() override {
        ResourceManager::Get().Init(core_, components_);

        if (core_) {
            core_->getPlayers().getPlayerConnectDispatcher().addEventHandler(this);
            core_->getPlayers().getPlayerSpawnDispatcher().addEventHandler(this);
            core_->getPlayers().getPlayerTextDispatcher().addEventHandler(this);
        }

        // Auto-start initial resources from configuration (e.g. resam_core, spawn_manager)
        ResourceManager::Get().EnsureResource("resam_core");
        ResourceManager::Get().EnsureResource("spawn_manager");
    }

    void onFree(IComponent* component) override {
        if (component == console_) {
            console_ = nullptr;
        }
    }

    void free() override {
        ResourceManager::Get().Shutdown();
        if (console_) {
            console_->getEventDispatcher().removeEventHandler(this);
        }
        if (core_) {
            core_->getPlayers().getPlayerConnectDispatcher().removeEventHandler(this);
            core_->getPlayers().getPlayerSpawnDispatcher().removeEventHandler(this);
            core_->getPlayers().getPlayerTextDispatcher().removeEventHandler(this);
        }
        delete this;
    }

    void reset() override {
        // Handle server GMX / reset
        ResourceManager::Get().Refresh();
    }

    // ConsoleEventHandler implementation
    bool onConsoleText(StringView command, StringView parameters, const ConsoleCommandSenderData& sender) override {
        std::string cmd(command.data(), command.length());
        std::string params(parameters.data(), parameters.length());

        if (cmd == "start") {
            if (params.empty()) {
                ResourceManager::Get().Log("Usage: start <resourceName>");
            } else {
                ResourceManager::Get().StartResource(params);
            }
            return true;
        } else if (cmd == "stop") {
            if (params.empty()) {
                ResourceManager::Get().Log("Usage: stop <resourceName>");
            } else {
                ResourceManager::Get().StopResource(params);
            }
            return true;
        } else if (cmd == "restart") {
            if (params.empty()) {
                ResourceManager::Get().Log("Usage: restart <resourceName>");
            } else {
                ResourceManager::Get().RestartResource(params);
            }
            return true;
        } else if (cmd == "ensure") {
            if (params.empty()) {
                ResourceManager::Get().Log("Usage: ensure <resourceName>");
            } else {
                ResourceManager::Get().EnsureResource(params);
            }
            return true;
        } else if (cmd == "refresh") {
            ResourceManager::Get().Refresh();
            return true;
        } else if (cmd == "resources") {
            const auto& list = ResourceManager::Get().GetResources();
            ResourceManager::Get().Log("--- Resa Resources (" + std::to_string(list.size()) + ") ---");
            for (const auto& pair : list) {
                std::string st = "stopped";
                if (pair.second->GetState() == ResourceState::Running) st = "running";
                else if (pair.second->GetState() == ResourceState::Starting) st = "starting";
                else if (pair.second->GetState() == ResourceState::Stopping) st = "stopping";
                ResourceManager::Get().Log("  - " + pair.first + " [" + st + "] v" + pair.second->GetMeta().version);
            }
            return true;
        }

        return false;
    }

    // PlayerConnectEventHandler implementation
    void onPlayerConnect(IPlayer& player) override {
        std::vector<LuaValue> args = { static_cast<int>(player.getID()) };
        EventManager::Get().TriggerEvent("playerConnect", args);
    }

    void onPlayerDisconnect(IPlayer& player, PeerDisconnectReason reason) override {
        std::vector<LuaValue> args = { static_cast<int>(player.getID()), static_cast<int>(reason) };
        EventManager::Get().TriggerEvent("playerDisconnect", args);
    }

    // PlayerSpawnEventHandler implementation
    void onPlayerSpawn(IPlayer& player) override {
        std::vector<LuaValue> args = { static_cast<int>(player.getID()) };
        EventManager::Get().TriggerEvent("playerSpawn", args);
    }

    // PlayerTextEventHandler implementation
    bool onPlayerText(IPlayer& player, StringView message) override {
        std::string msg(message.data(), message.length());
        std::vector<LuaValue> args = { static_cast<int>(player.getID()), msg };
        EventManager::Get().TriggerEvent("chatMessage", args);
        return true;
    }

    bool onPlayerCommandText(IPlayer& player, StringView message) override {
        std::string cmd(message.data(), message.length());
        std::vector<LuaValue> args = { static_cast<int>(player.getID()), cmd };
        EventManager::Get().TriggerEvent("playerCommand", args);
        return false;
    }

private:
    ICore* core_ = nullptr;
    IComponentList* components_ = nullptr;
    IConsoleComponent* console_ = nullptr;
};

} // namespace Resa
