#pragma once

#include "Utils.h"
#include <nlohmann/json.hpp>

struct RotationConfig {
    RE::FormID formID{0};
    float headingRotation{0.0f};
    std::optional<std::vector<float>> allowedAngles;
    std::string filePath;
};

class RotationHandler {
public:
    RotationHandler() {
        for (auto& cfg : LoadAllConfigs("Data\\SKSE\\Plugins\\DynamicWind\\Rotation")) {
            _configs[cfg.formID] = cfg;
        }
        logger::info("Loaded {} Rotation Configs", _configs.size());
    }

    std::unordered_map<RE::FormID, RotationConfig>& GetConfigs() { return _configs; }

    bool HasConfig(RE::FormID formID) { return _configs.contains(formID); }

    void AddNewConfig(const RE::FormID formID, const float headingRotation, std::optional<std::vector<float>> allowedAngles = std::nullopt) {
        RotationConfig cfg = {formID, headingRotation, allowedAngles};
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
                logger::info("Removed RotationConfig file for {:08X}", formID);
            }
        }
    }

    void Apply(RE::TESObjectREFR* ref, float windStrength, float windAngle, bool NewTargets = false) {
        auto it = _configs.find(ref->GetFormID());
        if (it == _configs.end()) {
            it = _configs.find(ref->GetBaseObject()->GetFormID());
            if (it == _configs.end()) return;
        }

        const auto& cfg = it->second;

        if (cfg.allowedAngles && NewTargets) {
            float baseAngle = cfg.headingRotation - windAngle;
            float baseAngleDeg = baseAngle * 180.0f / static_cast<float>(M_PI);

            baseAngleDeg = std::fmod(baseAngleDeg + 360.0f, 360.0f);

            float minDiff = 360.0f;
            float chosen = 0.0f;
            for (float allowed : *cfg.allowedAngles) {
                float diff = std::fabs(std::fmod(baseAngleDeg - allowed + 540.0f, 360.0f) - 180.0f);
                if (diff < minDiff) {
                    minDiff = diff;
                    chosen = allowed;
                }
            }

            auto currAngle = ref->GetAngle();
            const float targetAngle = chosen * static_cast<float>(M_PI) / 180.0f;
            if (currAngle.z == targetAngle) return;
            currAngle.z = targetAngle;
            ref->SetAngle(currAngle);
            ref->Update3DPosition(true);
            return;
        }

        if (!cfg.allowedAngles && !NewTargets) {
            auto currAngle = ref->GetAngle();
            const float targetAngle = cfg.headingRotation - windAngle;
            if (currAngle.z == targetAngle) return;
            currAngle.z = targetAngle;
            ref->SetAngle(currAngle);
            ref->Update3DPosition(true);
        }
    }

private:

    bool LoadConfig(const std::string& path, RotationConfig& outConfig) {
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

        RE::FormID formID = Utils::ParseForm(j["FormID"].get<std::string>());

        if (formID == 0) {
            logger::error("Failed to parse FormID {} '{}'", j["FormID"].get<std::string>(), outConfig.formID);
            return false;
        }

        outConfig.formID = formID;

        if (outConfig.formID == 0) {
            logger::error("Failed to parse FormID {} '{}'", j["FormID"].get<std::string>(), outConfig.formID);
            return false;
        }
        if (j.contains("headingRotation")) outConfig.headingRotation = j["headingRotation"].get<float>();

        if (j.contains("allowedAngles") && j["allowedAngles"].is_array()) {
            std::vector<float> angles;
            for (const auto& val : j["allowedAngles"]) {
                angles.push_back(val.get<float>());
            }
            outConfig.allowedAngles = angles;
        } else {
            outConfig.allowedAngles = std::nullopt;
        }

        return true;
    }

    void SaveConfigToFile(const RE::FormID formID, const RotationConfig& cfg) {
        std::filesystem::create_directories("Data\\SKSE\\Plugins\\DynamicWind\\Rotation");
        std::string path = std::format("Data\\SKSE\\Plugins\\DynamicWind\\Rotation\\{:08X}.json", formID);
        logger::info("Saving RotationConfig for {:08X}", formID);

        nlohmann::json j;
        j["FormID"] = Utils::FormIDToString(formID);
        j["headingRotation"] = cfg.headingRotation;
        if (cfg.allowedAngles) {
            j["allowedAngles"] = *cfg.allowedAngles;
        }
        std::ofstream file(path);
        if (!file.is_open()) {
            logger::error("Failed to open file for writing: {}", path);
            return;
        }
        file << j.dump(4);
    }

    std::vector<RotationConfig> LoadAllConfigs(const std::string& folder) {
        std::vector<RotationConfig> configs;

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

            RotationConfig cfg;
            if (LoadConfig(entry.path().string(), cfg)) {
                configs.push_back(cfg);
            }
        }
        return configs;
    }

    std::unordered_map<RE::FormID, RotationConfig> _configs;
};