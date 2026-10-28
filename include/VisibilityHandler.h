#pragma once

struct VisibilityConfig {
    RE::FormID formID{0};
    float minVisibility{0.0f};
    float maxVisibility{1.0f};
    float minWindStrength{0.0f};
    float maxWindStrength{1.0f};
    float headingRotation{0.0f};
    float angleFactor{0.0f};
    std::string filePath;
};

class VisibilityHandler {
public:
    VisibilityHandler() {
        for (auto& cfg : LoadAllConfigs("Data\\SKSE\\Plugins\\DynamicWind\\Visibility")) {
            _configs[cfg.formID] = cfg;
        }
        logger::info("Loaded {} Visibility Configs", _configs.size());
    }

    std::unordered_map<RE::FormID, VisibilityConfig>& GetConfigs() { return _configs; }

    bool HasConfig(RE::FormID formID) { return _configs.contains(formID); }

    void AddNewConfig(RE::FormID formID, float minVisibility, float maxVisibility, float minWindStrength,
                      float maxWindStrength, float headingRotation, float angleFactor) {
        VisibilityConfig cfg = {formID,          minVisibility,   maxVisibility, minWindStrength,
                                maxWindStrength, headingRotation, angleFactor};
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
                logger::info("Removed VisibilityConfig file for {:08X}", formID);
            }
        }
    }

    void Apply(RE::TESObjectREFR* ref, float windStrength, float windAngle) {
        auto* node = ref->Get3D();
        if (!node) return;

        auto it = _configs.find(ref->GetFormID());
        if (it == _configs.end()) {
            it = _configs.find(ref->GetBaseObject()->GetFormID());
            if (it == _configs.end()) return;
        }

        const auto& cfg = it->second;

        float objectAngle = ref->GetAngle().z + cfg.headingRotation;
        float delta = windAngle + objectAngle;
        while (delta > M_PI) delta -= M_PI * 2.0f;
        while (delta < -M_PI) delta += M_PI * 2.0f;
        float alignment = std::cos(delta);

        float finalFactor = 1.0f;
        if (cfg.angleFactor != 0.0f) {
            finalFactor = 1.0f - cfg.angleFactor + (alignment * cfg.angleFactor);
            finalFactor = std::clamp(finalFactor, 0.0f, 1.0f);
        }

        float visibility = 1.0f;
        float wind = windStrength * finalFactor;
        if (wind <= cfg.minWindStrength) {
            visibility = cfg.minVisibility;
        } else if (wind >= cfg.maxWindStrength) {
            visibility = cfg.maxVisibility;
        } else {
            float t = (wind - cfg.minWindStrength) / (cfg.maxWindStrength - cfg.minWindStrength);
            visibility = cfg.minVisibility + t * (cfg.maxVisibility - cfg.minVisibility);
        }

        if (auto* fadeNode = node->AsFadeNode()) {
            fadeNode->GetRuntimeData().currentFade = visibility;
        }
    }

private:
    void SaveConfigToFile(const RE::FormID formID, const VisibilityConfig& cfg) {
        std::filesystem::create_directories("Data\\SKSE\\Plugins\\DynamicWind\\Visibility");
        std::string path = std::format("Data\\SKSE\\Plugins\\DynamicWind\\Visibility\\{:08X}.json", formID);

        logger::info("Saving VisibilityConfig for {:08X}", formID);

        nlohmann::json j;
        j["FormID"] = Utils::FormIDToString(formID);
        j["minVisibility"] = cfg.minVisibility;
        j["maxVisibility"] = cfg.maxVisibility;
        j["minWindStrength"] = cfg.minWindStrength;
        j["maxWindStrength"] = cfg.maxWindStrength;
        j["headingRotation"] = cfg.headingRotation;
        j["angleFactor"] = cfg.angleFactor;
        std::ofstream file(path);
        if (!file.is_open()) {
            logger::error("Failed to open file for writing: {}", path);
            return;
        }
        file << j.dump(4);
    }

    bool LoadConfig(const std::string& path, VisibilityConfig& outConfig) {
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
        if (j.contains("minVisibility")) outConfig.minVisibility = j["minVisibility"].get<float>();
        if (j.contains("maxVisibility")) outConfig.maxVisibility = j["maxVisibility"].get<float>();
        if (j.contains("minWindStrength")) outConfig.minWindStrength = j["minWindStrength"].get<float>();
        if (j.contains("maxWindStrength")) outConfig.maxWindStrength = j["maxWindStrength"].get<float>();
        if (j.contains("headingRotation")) outConfig.headingRotation = j["headingRotation"].get<float>();
        if (j.contains("angleFactor")) outConfig.angleFactor = j["angleFactor"].get<float>();

        return true;
    }

    std::vector<VisibilityConfig> LoadAllConfigs(const std::string& folder) {
        std::vector<VisibilityConfig> configs;

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

            VisibilityConfig cfg;
            if (LoadConfig(entry.path().string(), cfg)) {
                configs.push_back(cfg);
            }
        }
        return configs;
    }

    std::unordered_map<RE::FormID, VisibilityConfig> _configs;
};
