/*
 *  resa-multiplayer (resam)
 *  Component Header
 */

#pragma once

#include "lua_defs.hpp"
#include "resource.hpp"
#include "resource_manager.hpp"
#include "event_manager.hpp"
#include "bindings.hpp"
#include <Server/Components/Console/console.hpp>
#include <Server/Components/Dialogs/dialogs.hpp>
#include <Server/Components/Vehicles/vehicles.hpp>
#include <Server/Components/Pickups/pickups.hpp>
#include <Server/Components/TextLabels/textlabels.hpp>
#include <Server/Components/Objects/objects.hpp>
#include <Server/Components/Timers/timers.hpp>
#include <Server/Components/Checkpoints/checkpoints.hpp>
#include <Server/Components/Actors/actors.hpp>
#include <Server/Components/Variables/variables.hpp>
#include <Server/Components/Classes/classes.hpp>

namespace Resa {

class LuaComponent final : public IComponent,
                           public ConsoleEventHandler,
                           public PlayerConnectEventHandler,
                           public PlayerSpawnEventHandler,
                           public PlayerTextEventHandler,
                           public PlayerShotEventHandler,
                           public PlayerDamageEventHandler,
                           public PlayerChangeEventHandler,
                           public PlayerClickEventHandler,
                           public VehicleEventHandler,
                           public PickupEventHandler,
                           public PlayerCheckpointEventHandler,
                           public ActorEventHandler,
                           public ObjectEventHandler,
                           public ClassEventHandler,
                           public PlayerDialogEventHandler {
public:
    PROVIDE_UID(Lua_UID)

    LuaComponent() = default;
    ~LuaComponent() override = default;

    StringView componentName() const override {
        return "Lua";
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
        dialogs_ = components->queryComponent<IDialogsComponent>();
        if (dialogs_) {
            dialogs_->getEventDispatcher().addEventHandler(this);
        }
        vehicles_ = components->queryComponent<IVehiclesComponent>();
        if (vehicles_) {
            vehicles_->getEventDispatcher().addEventHandler(this);
        }
        pickups_ = components->queryComponent<IPickupsComponent>();
        if (pickups_) {
            pickups_->getEventDispatcher().addEventHandler(this);
        }
        checkpoints_ = components->queryComponent<ICheckpointsComponent>();
        if (checkpoints_) {
            checkpoints_->getEventDispatcher().addEventHandler(this);
        }
        actors_ = components->queryComponent<IActorsComponent>();
        if (actors_) {
            actors_->getEventDispatcher().addEventHandler(this);
        }
        objects_ = components->queryComponent<IObjectsComponent>();
        if (objects_) {
            objects_->getEventDispatcher().addEventHandler(this);
        }
        classes_ = components->queryComponent<IClassesComponent>();
        if (classes_) {
            classes_->getEventDispatcher().addEventHandler(this);
        }
    }

    void onReady() override {
        ResourceManager::Get().Init(core_, components_);

        if (core_) {
            core_->getPlayers().getPlayerConnectDispatcher().addEventHandler(this);
            core_->getPlayers().getPlayerSpawnDispatcher().addEventHandler(this);
            core_->getPlayers().getPlayerTextDispatcher().addEventHandler(this);
            core_->getPlayers().getPlayerShotDispatcher().addEventHandler(this);
            core_->getPlayers().getPlayerDamageDispatcher().addEventHandler(this);
            core_->getPlayers().getPlayerChangeDispatcher().addEventHandler(this);
            core_->getPlayers().getPlayerClickDispatcher().addEventHandler(this);
        }

        // Auto-start initial resources from configuration (e.g. resam_core, spawn_manager)
        ResourceManager::Get().EnsureResource("resam_core");
        ResourceManager::Get().EnsureResource("spawn_manager");
        ResourceManager::Get().EnsureResource("resam_admin");
        ResourceManager::Get().EnsureResource("resam_vehicles");
    }

    void onFree(IComponent* component) override {
        if (component == console_) {
            console_ = nullptr;
        }
        if (component == dialogs_) {
            dialogs_ = nullptr;
        }
        if (component == vehicles_) {
            vehicles_ = nullptr;
        }
        if (component == pickups_) {
            pickups_ = nullptr;
        }
        if (component == checkpoints_) {
            checkpoints_ = nullptr;
        }
        if (component == actors_) {
            actors_ = nullptr;
        }
    }

    void free() override {
        ResourceManager::Get().Shutdown();
        if (console_) {
            console_->getEventDispatcher().removeEventHandler(this);
        }
        if (dialogs_) {
            dialogs_->getEventDispatcher().removeEventHandler(this);
        }
        if (vehicles_) {
            vehicles_->getEventDispatcher().removeEventHandler(this);
        }
        if (pickups_) {
            pickups_->getEventDispatcher().removeEventHandler(this);
        }
        if (checkpoints_) {
            checkpoints_->getEventDispatcher().removeEventHandler(this);
        }
        if (actors_) {
            actors_->getEventDispatcher().removeEventHandler(this);
        }
        if (core_) {
            core_->getPlayers().getPlayerConnectDispatcher().removeEventHandler(this);
            core_->getPlayers().getPlayerSpawnDispatcher().removeEventHandler(this);
            core_->getPlayers().getPlayerTextDispatcher().removeEventHandler(this);
            core_->getPlayers().getPlayerDamageDispatcher().removeEventHandler(this);
            core_->getPlayers().getPlayerChangeDispatcher().removeEventHandler(this);
            core_->getPlayers().getPlayerClickDispatcher().removeEventHandler(this);
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
    bool onPlayerRequestSpawn(IPlayer& player) override {
        std::vector<LuaValue> args = { static_cast<int>(player.getID()) };
        EventManager::Get().TriggerEvent("playerRequestSpawn", args);
        return true;
    }

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
        DispatchCommand(static_cast<int>(player.getID()), cmd);
        std::vector<LuaValue> args = { static_cast<int>(player.getID()), cmd };
        EventManager::Get().TriggerEvent("playerCommand", args);
        return true;
    }

    // PlayerDamageEventHandler implementation
    void onPlayerDeath(IPlayer& player, IPlayer* killer, int reason) override {
        int killerId = killer ? static_cast<int>(killer->getID()) : -1;
        std::vector<LuaValue> args = {
            static_cast<int>(player.getID()),
            killerId,
            reason
        };
        EventManager::Get().TriggerEvent("playerDeath", args);
    }

    void onPlayerTakeDamage(IPlayer& player, IPlayer* from, float amount, unsigned weapon, BodyPart part) override {
        int fromId = from ? static_cast<int>(from->getID()) : -1;
        std::vector<LuaValue> args = {
            static_cast<int>(player.getID()),
            fromId,
            static_cast<double>(amount),
            static_cast<int>(weapon),
            static_cast<int>(part)
        };
        EventManager::Get().TriggerEvent("playerTakeDamage", args);
    }

    void onPlayerGiveDamage(IPlayer& player, IPlayer& to, float amount, unsigned weapon, BodyPart part) override {
        std::vector<LuaValue> args = {
            static_cast<int>(player.getID()),
            static_cast<int>(to.getID()),
            static_cast<double>(amount),
            static_cast<int>(weapon),
            static_cast<int>(part)
        };
        EventManager::Get().TriggerEvent("playerGiveDamage", args);
    }

    // PlayerChangeEventHandler implementation
    void onPlayerKeyStateChange(IPlayer& player, uint32_t newKeys, uint32_t oldKeys) override {
        std::vector<LuaValue> args = {
            static_cast<int>(player.getID()),
            static_cast<int>(newKeys),
            static_cast<int>(oldKeys)
        };
        EventManager::Get().TriggerEvent("playerKeyStateChange", args);
    }

    void onPlayerStateChange(IPlayer& player, PlayerState newState, PlayerState oldState) override {
        std::vector<LuaValue> args = {
            static_cast<int>(player.getID()),
            static_cast<int>(newState),
            static_cast<int>(oldState)
        };
        EventManager::Get().TriggerEvent("playerStateChange", args);
    }

    // PlayerClickEventHandler implementation
    void onPlayerClickMap(IPlayer& player, Vector3 pos) override {
        std::vector<LuaValue> args = {
            static_cast<int>(player.getID()),
            static_cast<double>(pos.x),
            static_cast<double>(pos.y),
            static_cast<double>(pos.z)
        };
        EventManager::Get().TriggerEvent("playerClickMap", args);
    }

    void onPlayerClickPlayer(IPlayer& player, IPlayer& clicked, PlayerClickSource source) override {
        std::vector<LuaValue> args = {
            static_cast<int>(player.getID()),
            static_cast<int>(clicked.getID()),
            static_cast<int>(source)
        };
        EventManager::Get().TriggerEvent("playerClickPlayer", args);
    }

    // PlayerShotEventHandler implementation
    bool onPlayerShotMissed(IPlayer& player, const PlayerBulletData& bulletData) override {
        std::vector<LuaValue> args = {
            static_cast<int>(player.getID()),
            static_cast<int>(bulletData.weapon),
            0,
            0,
            static_cast<double>(bulletData.hitPos.x),
            static_cast<double>(bulletData.hitPos.y),
            static_cast<double>(bulletData.hitPos.z)
        };
        EventManager::Get().TriggerEvent("playerWeaponShot", args);
        return true;
    }

    bool onPlayerShotPlayer(IPlayer& player, IPlayer& target, const PlayerBulletData& bulletData) override {
        std::vector<LuaValue> args = {
            static_cast<int>(player.getID()),
            static_cast<int>(bulletData.weapon),
            1,
            static_cast<int>(target.getID()),
            static_cast<double>(bulletData.hitPos.x),
            static_cast<double>(bulletData.hitPos.y),
            static_cast<double>(bulletData.hitPos.z)
        };
        EventManager::Get().TriggerEvent("playerWeaponShot", args);
        return true;
    }

    bool onPlayerShotVehicle(IPlayer& player, IVehicle& target, const PlayerBulletData& bulletData) override {
        std::vector<LuaValue> args = {
            static_cast<int>(player.getID()),
            static_cast<int>(bulletData.weapon),
            2,
            static_cast<int>(target.getID()),
            static_cast<double>(bulletData.hitPos.x),
            static_cast<double>(bulletData.hitPos.y),
            static_cast<double>(bulletData.hitPos.z)
        };
        EventManager::Get().TriggerEvent("playerWeaponShot", args);
        return true;
    }

    bool onPlayerShotObject(IPlayer& player, IObject& target, const PlayerBulletData& bulletData) override {
        std::vector<LuaValue> args = {
            static_cast<int>(player.getID()),
            static_cast<int>(bulletData.weapon),
            3,
            static_cast<int>(target.getID()),
            static_cast<double>(bulletData.hitPos.x),
            static_cast<double>(bulletData.hitPos.y),
            static_cast<double>(bulletData.hitPos.z)
        };
        EventManager::Get().TriggerEvent("playerWeaponShot", args);
        return true;
    }

    bool onPlayerShotPlayerObject(IPlayer& player, IPlayerObject& target, const PlayerBulletData& bulletData) override {
        std::vector<LuaValue> args = {
            static_cast<int>(player.getID()),
            static_cast<int>(bulletData.weapon),
            4,
            static_cast<int>(target.getID()),
            static_cast<double>(bulletData.hitPos.x),
            static_cast<double>(bulletData.hitPos.y),
            static_cast<double>(bulletData.hitPos.z)
        };
        EventManager::Get().TriggerEvent("playerWeaponShot", args);
        return true;
    }

    // VehicleEventHandler implementation
    void onPlayerEnterVehicle(IPlayer& player, IVehicle& vehicle, bool passenger) override {
        std::vector<LuaValue> args = {
            static_cast<int>(player.getID()),
            static_cast<int>(vehicle.getID()),
            passenger
        };
        EventManager::Get().TriggerEvent("playerEnterVehicle", args);
    }

    void onPlayerExitVehicle(IPlayer& player, IVehicle& vehicle) override {
        std::vector<LuaValue> args = {
            static_cast<int>(player.getID()),
            static_cast<int>(vehicle.getID())
        };
        EventManager::Get().TriggerEvent("playerExitVehicle", args);
    }

    void onVehicleDeath(IVehicle& vehicle, IPlayer& player) override {
        std::vector<LuaValue> args = {
            static_cast<int>(vehicle.getID()),
            static_cast<int>(player.getID())
        };
        EventManager::Get().TriggerEvent("vehicleDeath", args);
    }

    bool onVehicleMod(IPlayer& player, IVehicle& vehicle, int component) override {
        std::vector<LuaValue> args = {
            static_cast<int>(player.getID()),
            static_cast<int>(vehicle.getID()),
            component
        };
        EventManager::Get().TriggerEvent("vehicleMod", args);
        return true;
    }

    bool onVehiclePaintJob(IPlayer& player, IVehicle& vehicle, int paintJob) override {
        std::vector<LuaValue> args = {
            static_cast<int>(player.getID()),
            static_cast<int>(vehicle.getID()),
            paintJob
        };
        EventManager::Get().TriggerEvent("vehiclePaintjob", args);
        return true;
    }

    bool onVehicleRespray(IPlayer& player, IVehicle& vehicle, int colour1, int colour2) override {
        std::vector<LuaValue> args = {
            static_cast<int>(player.getID()),
            static_cast<int>(vehicle.getID()),
            colour1,
            colour2
        };
        EventManager::Get().TriggerEvent("vehicleRespray", args);
        return true;
    }

    void onVehicleSpawn(IVehicle& vehicle) override {
        std::vector<LuaValue> args = { static_cast<int>(vehicle.getID()) };
        EventManager::Get().TriggerEvent("vehicleSpawn", args);
    }

    void onEnterExitModShop(IPlayer& player, bool enterexit, int interiorID) override {
        std::vector<LuaValue> args = {
            static_cast<int>(player.getID()),
            enterexit,
            interiorID
        };
        EventManager::Get().TriggerEvent("enterExitModShop", args);
    }

    // PickupEventHandler implementation
    void onPlayerPickUpPickup(IPlayer& player, IPickup& pickup) override {
        std::vector<LuaValue> args = {
            static_cast<int>(player.getID()),
            static_cast<int>(pickup.getID())
        };
        EventManager::Get().TriggerEvent("playerPickUpPickup", args);
    }

    // PlayerCheckpointEventHandler implementation
    void onPlayerEnterCheckpoint(IPlayer& player) override {
        std::vector<LuaValue> args = { static_cast<int>(player.getID()) };
        EventManager::Get().TriggerEvent("playerEnterCheckpoint", args);
    }

    void onPlayerLeaveCheckpoint(IPlayer& player) override {
        std::vector<LuaValue> args = { static_cast<int>(player.getID()) };
        EventManager::Get().TriggerEvent("playerLeaveCheckpoint", args);
    }

    void onPlayerEnterRaceCheckpoint(IPlayer& player) override {
        std::vector<LuaValue> args = { static_cast<int>(player.getID()) };
        EventManager::Get().TriggerEvent("playerEnterRaceCheckpoint", args);
    }

    void onPlayerLeaveRaceCheckpoint(IPlayer& player) override {
        std::vector<LuaValue> args = { static_cast<int>(player.getID()) };
        EventManager::Get().TriggerEvent("playerLeaveRaceCheckpoint", args);
    }

    // ActorEventHandler implementation
    void onPlayerGiveDamageActor(IPlayer& player, IActor& actor, float amount, unsigned weapon, BodyPart part) override {
        std::vector<LuaValue> args = {
            static_cast<int>(player.getID()),
            static_cast<int>(actor.getID()),
            static_cast<double>(amount),
            static_cast<int>(weapon),
            static_cast<int>(part)
        };
        EventManager::Get().TriggerEvent("playerGiveDamageActor", args);
    }

    // ObjectEventHandler implementation
    void onMoved(IObject& object) override {
        std::vector<LuaValue> args = { static_cast<int>(object.getID()) };
        EventManager::Get().TriggerEvent("objectMoved", args);
    }

    void onPlayerObjectMoved(IPlayer& player, IPlayerObject& object) override {
        std::vector<LuaValue> args = {
            static_cast<int>(player.getID()),
            static_cast<int>(object.getID())
        };
        EventManager::Get().TriggerEvent("playerObjectMoved", args);
    }

    // ClassEventHandler implementation
    bool onPlayerRequestClass(IPlayer& player, unsigned int classId) override {
        std::vector<LuaValue> args = {
            static_cast<int>(player.getID()),
            static_cast<int>(classId)
        };
        EventManager::Get().TriggerEvent("playerRequestClass", args);
        return true;
    }

    // PlayerDialogEventHandler implementation
    void onDialogResponse(IPlayer& player, int dialogId, DialogResponse response, int listItem, StringView inputText) override {
        std::string text(inputText.data(), inputText.length());
        std::vector<LuaValue> args = {
            static_cast<int>(player.getID()),
            dialogId,
            static_cast<int>(response),
            listItem,
            text
        };
        EventManager::Get().TriggerEvent("playerDialogResponse", args);
    }

private:
    ICore* core_ = nullptr;
    IComponentList* components_ = nullptr;
    IConsoleComponent* console_ = nullptr;
    IDialogsComponent* dialogs_ = nullptr;
    IVehiclesComponent* vehicles_ = nullptr;
    IPickupsComponent* pickups_ = nullptr;
    ICheckpointsComponent* checkpoints_ = nullptr;
    IActorsComponent* actors_ = nullptr;
    IObjectsComponent* objects_ = nullptr;
    IClassesComponent* classes_ = nullptr;
};

} // namespace Resa
