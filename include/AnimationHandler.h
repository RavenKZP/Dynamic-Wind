#pragma once

#include "Utils.h"
#include <algorithm>
#include <cctype>
#include <string_view>
#include <nlohmann/json.hpp>

struct AnimationConfig {
    RE::FormID formID{0};
    float speedMin{0.0f};
    float speedMax{1.0f};
    float headingRotation{0.0f};
    float angleFactor{0.0f};
    std::string filePath;
};

struct AnimationUpdateDiagnostics {
    float speed{};
    uint32_t controllerWrites{};
    bool evaluated{};
};

class AnimationHandler{
public:
    AnimationHandler() {
        for (auto& cfg : LoadAllConfigs("Data\\SKSE\\Plugins\\DynamicWind\\Animation")) {
            _configs[cfg.formID] = cfg;
        }
        RebuildModelConfigs();
        logger::info("Loaded {} Animation Configs", _configs.size());
    }

    std::unordered_map<RE::FormID, AnimationConfig>& GetConfigs() { return _configs; }

    bool HasConfig(RE::FormID formID) { return _configs.contains(formID); }

    bool HasConfig(RE::TESObjectREFR* ref) { return FindConfig(ref) != nullptr; }

    /// Returns the same matching rule used when applying animation speed.
    const AnimationConfig* GetMatchingConfig(RE::TESObjectREFR* ref) const { return FindConfig(ref); }

    /// Returns the number of NIF paths indexed for animation matching.
    size_t GetModelConfigCount() const { return _modelConfigs.size(); }

    void AddNewConfig(const RE::FormID formID, float speedMin, float speedMax, float headingRotation,
                      float angleFactor) {
        AnimationConfig cfg = {formID, speedMin, speedMax, headingRotation, angleFactor};
        _configs[cfg.formID] = cfg;
        RebuildModelConfigs();

        SaveConfigToFile(cfg.formID, cfg);
    }

    void RemoveConfig(RE::FormID formID) {
        auto it = _configs.find(formID);
        if (it != _configs.end()) {
            std::string path = _configs[formID].filePath;
            _configs.erase(formID);
            RebuildModelConfigs();
            if (std::filesystem::exists(path)) {
                std::filesystem::remove(path);
                logger::info("Removed AnimationConfig file for {:08X}", formID);
            }
        }
    }

    void Apply(RE::TESObjectREFR* ref, float windStrength, float windAngle,
               AnimationUpdateDiagnostics* diagnostics = nullptr) {
        if (diagnostics) *diagnostics = {};
        auto* node = ref->Get3D();
        if (!node) return;

        const auto* config = FindConfig(ref);
        if (!config) return;
        const auto& cfg = *config;

        float objectAngle = ref->GetAngle().z + cfg.headingRotation;
        float delta = windAngle + objectAngle;
        while (delta > M_PI) delta -= M_PI * 2.0f;
        while (delta < -M_PI) delta += M_PI * 2.0f;
        float alignment = std::cos(delta);

        float finalFactor = 1.0f;
        if (cfg.angleFactor > 0.0f) {
            finalFactor = 1.0f - cfg.angleFactor + (alignment * cfg.angleFactor);
        }

        float speed = cfg.speedMin + ((cfg.speedMax - cfg.speedMin) * windStrength * finalFactor);

        if (diagnostics) {
            diagnostics->speed = speed;
            diagnostics->evaluated = true;
        }
        Utils::ApplySpeedToNode(node, speed, diagnostics ? &diagnostics->controllerWrites : nullptr);
    }

    private:

    static std::string NormalizeModelPath(std::string path) {
        std::replace(path.begin(), path.end(), '/', '\\');
        std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (path.starts_with("meshes\\")) path.erase(0, sizeof("meshes\\") - 1);
        return path;
    }

    void RebuildModelConfigs() {
        _modelConfigs.clear();
        for (const auto& [formID, cfg] : _configs) {
            auto* base = RE::TESForm::LookupByID<RE::TESBoundObject>(formID);
            if (!base) continue;
            auto path = NormalizeModelPath(Utils::GetModelPath(base));
            if (path.empty()) continue;
            const auto [entry, inserted] = _modelConfigs.try_emplace(std::move(path), formID);
            if (!inserted && formID < entry->second) entry->second = formID;
        }
    }

    const AnimationConfig* FindConfig(RE::TESObjectREFR* ref) const {
        auto it = _configs.find(ref->GetFormID());
        if (it != _configs.end()) return &it->second;
        it = _configs.find(ref->GetBaseObject()->GetFormID());
        if (it != _configs.end()) return &it->second;
        const auto path = NormalizeModelPath(Utils::GetModelPath(ref->GetBaseObject()));
        const auto model = _modelConfigs.find(path);
        if (model != _modelConfigs.end()) return &_configs.at(model->second);
        const std::string_view modelPath{path};
        const auto separator = modelPath.find_last_of('\\');
        const auto directoryLength = separator == std::string_view::npos ? 0 : separator + 1;
        const auto directory = modelPath.substr(0, directoryLength);
        const auto filename = modelPath.substr(directoryLength);
        if (filename.empty()) return nullptr;
        const AnimationConfig* match = nullptr;
        for (const auto& [candidate, formID] : _modelConfigs) {
            const std::string_view candidatePath{candidate};
            const auto candidateSeparator = candidatePath.find_last_of('\\');
            const auto candidateDirectoryLength = candidateSeparator == std::string_view::npos ? 0 : candidateSeparator + 1;
            if (candidatePath.substr(0, candidateDirectoryLength) != directory) continue;
            const auto candidateFilename = candidatePath.substr(candidateDirectoryLength);
            if (candidateFilename.empty() || (!filename.ends_with(candidateFilename) && !candidateFilename.ends_with(filename))) continue;
            if (match && match->formID != formID) return nullptr;
            match = &_configs.at(formID);
        }
        return match;
    }

    bool LoadConfig(const std::string& path, AnimationConfig& outConfig) {
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
        if (j.contains("speedMin")) outConfig.speedMin = j["speedMin"].get<float>();
        if (j.contains("speedMax")) outConfig.speedMax = j["speedMax"].get<float>();
        if (j.contains("headingRotation")) outConfig.headingRotation = j["headingRotation"].get<float>();
        if (j.contains("angleFactor")) outConfig.angleFactor = j["angleFactor"].get<float>();
        outConfig.filePath = path;
        return true;
    }

    void SaveConfigToFile(const RE::FormID formID, const AnimationConfig& cfg) {
        std::filesystem::create_directories("Data\\SKSE\\Plugins\\DynamicWind\\Animation");
        std::string path = std::format("Data\\SKSE\\Plugins\\DynamicWind\\Animation\\{:08X}.json", formID);

            logger::info("Saving AnimationConfig for {:08X}", formID);
        
        nlohmann::json j;
        j["FormID"] = Utils::FormIDToString(formID);
        j["speedMin"] = cfg.speedMin;
        j["speedMax"] = cfg.speedMax;
        j["headingRotation"] = cfg.headingRotation;
        j["angleFactor"] = cfg.angleFactor;
        std::ofstream file(path);
        if (!file.is_open()) {
            logger::error("Failed to open file for writing: {}", path);
            return;
        }
        file << j.dump(4);
    }

    std::vector<AnimationConfig> LoadAllConfigs(const std::string& folder) {
        std::vector<AnimationConfig> configs;

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

            AnimationConfig cfg;
            if (LoadConfig(entry.path().string(), cfg)) {
                configs.push_back(cfg);
            }
        }
        return configs;
    }

    std::unordered_map<RE::FormID, AnimationConfig> _configs;
    std::unordered_map<std::string, RE::FormID> _modelConfigs;
};
