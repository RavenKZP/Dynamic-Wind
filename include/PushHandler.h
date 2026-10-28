#pragma once

#include "Utils.h"
#include "DynamicWindAPI.h"
#include <algorithm>
#include <nlohmann/json.hpp>

struct PushConfig {
    RE::FormID formID{0};
    float windSensitivity{1.0f};
    std::string filePath;
};

class PushHandler {
public:
    PushHandler() {
        for (auto& cfg : LoadAllConfigs("Data\\SKSE\\Plugins\\DynamicWind\\Push\\")) {
            _configs[cfg.formID] = cfg;
        }
        logger::info("Loaded {} Push Configs", _configs.size());
    }

    std::unordered_map<RE::FormID, PushConfig>& GetConfigs() { return _configs; }

    bool HasConfig(RE::FormID formID) { return _configs.contains(formID); }

    void AddNewConfig(const RE::FormID formID, float windSensitivity) {
        PushConfig cfg = {formID, windSensitivity};
        _configs[cfg.formID] = cfg;

        SaveConfigToFile(cfg.formID, cfg);
    }

    void RemoveConfig(RE::FormID formID) {
        auto it = _configs.find(formID);
        if (it != _configs.end()) {
            std::string path = _configs[formID].filePath;
            _configs.erase(formID);
            if (std::filesystem::exists(path)) {
                std::filesystem::remove(path);
                logger::info("Removed PushConfig file for {:08X}", formID);
            }
        }
    }

    void Apply(RE::TESObjectREFR* ref, float windStrength, float windAngle, float deltaTime) {
        auto* node = ref->Get3D();
        if (!node) return;

        auto it = _configs.find(ref->GetFormID());
        if (it == _configs.end()) {
            it = _configs.find(ref->GetBaseObject()->GetFormID());
            if (it == _configs.end()) return;
        }

        const auto& cfg = it->second;
        float nativeScale = float(std::min(1.0, std::pow(windStrength, 3.0)));
        RE::hkVector4 nativeVelocity{nativeScale * cfg.windSensitivity * std::cos(windAngle),
                                    nativeScale * cfg.windSensitivity * std::sin(windAngle), 0.0f, 0.0f};
        const float scale = cfg.windSensitivity * LinearForceScale * std::min(deltaTime, MaximumTimeStep);
        Utils::ApplyImpulseToNode(node, nativeVelocity, {{windStrength, windAngle}, deltaTime}, ref->GetFormID(), scale);
    }

private:
    static constexpr float ReferenceUpdateRate = 60.0f;
    static constexpr float LinearForceScale = ReferenceUpdateRate * DynamicWindAPI::PushReferenceStrength * DynamicWindAPI::PushReferenceStrength;
    static constexpr float MaximumTimeStep = 0.1f;

    void SaveConfigToFile(const RE::FormID formID, const PushConfig& cfg) {
        std::filesystem::create_directories("Data\\SKSE\\Plugins\\DynamicWind\\Push");
        std::string path = std::format("Data\\SKSE\\Plugins\\DynamicWind\\Push\\{:08X}.json", formID);

        logger::info("Saving PushConfig for {:08X}", formID);

        nlohmann::json j;
        j["FormID"] = Utils::FormIDToString(formID);
        j["windSensitivity"] = cfg.windSensitivity;
        std::ofstream file(path);
        if (!file.is_open()) {
            logger::error("Failed to open file for writing: {}", path);
            return;
        }
        file << j.dump(4);
    }

    bool LoadConfig(const std::string& path, PushConfig& outConfig) {
        std::ifstream file(path);
        if (!file.is_open()) {
            return false;
        }

        nlohmann::json j;
        file >> j;

        if (!j.contains("FormID")) {
            logger::error("file dont contain FormID");
            return false;
        }

        auto formID = Utils::ParseForm(j["FormID"].get<std::string>());

        if (formID == 0) {
            logger::error("Failed to parse FormID {} '{}'", j["FormID"].get<std::string>(), outConfig.formID);
            return false;
        }

        outConfig.formID = formID;
        outConfig.filePath = path;

        if (outConfig.formID == 0) {
            logger::error("Failed to parse FormID {} '{}'", j["FormID"].get<std::string>(), outConfig.formID);
            return false;
        }
        if (j.contains("windSensitivity")) outConfig.windSensitivity = j["windSensitivity"].get<float>();

        return true;
    }

    std::vector<PushConfig> LoadAllConfigs(const std::string& folder) {
        std::vector<PushConfig> configs;

        if (!std::filesystem::exists(folder) || !std::filesystem::is_directory(folder)) {
            logger::error("{} not exists or is not directory", folder);
            return configs;
        }

        for (const auto& entry : std::filesystem::directory_iterator(folder)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            if (entry.path().extension() != ".json") {
                continue;
            }

            logger::info("Reading {}", entry.path().string());

            PushConfig cfg;
            if (LoadConfig(entry.path().string(), cfg)) {
                configs.push_back(cfg);
            }
        }
        return configs;
    }

    std::unordered_map<RE::FormID, PushConfig> _configs;
};