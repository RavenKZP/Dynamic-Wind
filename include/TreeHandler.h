#pragma once

#include "Utils.h"
#include <nlohmann/json.hpp>

struct TreeDataConfig {
    float trunkFlexibility{0};
    float branchFlexibility{0};
    float trunkAmplitude{0};
    float frontAmplitude{0};
    float backAmplitude{0};
    float sideAmplitude{0};
    float frontFrequency{0};
    float backFrequency{0};
    float sideFrequency{0};
    float leafFlexibility{0};
    float leafAmplitude{0};
    float leafFrequency{0};
};

struct TreeConfig {
    RE::FormID formID{0};
    TreeDataConfig min;
    TreeDataConfig max;
    std::string filePath;
};

class TreeHandler {
public:
    TreeHandler() {
        for (auto& cfg : LoadAllConfigs("Data\\SKSE\\Plugins\\DynamicWind\\Trees")) {
            _configs[cfg.formID] = cfg;
        }
        logger::info("Loaded {} Tree Configs", _configs.size());
    }

    void Update(float windStrength, float windAngle) {

        // Love it was there since always, but never used
        auto* treeMgr = RE::BSTreeManager::GetSingleton();
        if (treeMgr) {
            float treeDir = windAngle - M_PI;
            RE::NiPoint2 treeWindDir = {std::cos(treeDir), std::sin(treeDir)};
            treeMgr->windDirection = treeWindDir;
            // tree data (Amplitude and Flexibility) works much better
            // treeMgr->windMagnitude = _targetStrength / 10.0f;
        }

        for (auto& [formID, cfg] : _configs) {
            auto* tree = RE::TESForm::LookupByID<RE::TESObjectTREE>(formID);
            if (!tree) continue;

            Apply(tree, cfg, windStrength);
        }
    }

    std::unordered_map<RE::FormID, TreeConfig>& GetConfigs() { return _configs; }

    bool HasConfig(RE::FormID formID) { return _configs.contains(formID); }

    void AddNewConfig(const RE::TESObjectTREE* tree, const TreeDataConfig& cfg) {
        TreeConfig treeCfg;

        treeCfg.formID = tree->GetFormID();
        treeCfg.max = cfg;

        _configs[treeCfg.formID] = treeCfg;
        
        SaveConfigToFile(treeCfg.formID, cfg, tree->GetFormEditorID());
    }

    TreeConfig GetConfig(RE::FormID formID) {
        if (_configs.contains(formID)) {
            return _configs[formID];
        }
        return TreeConfig{};
    }

    void RemoveConfig(RE::FormID formID) {
        auto it = _configs.find(formID);
        if (it != _configs.end()) {
            std::string path = _configs[formID].filePath;
            _configs.erase(formID);
            if (std::filesystem::exists(path)) {
                std::filesystem::remove(path);
                logger::info("Removed TreeConfig file for {:08X}", formID);
            }
        }
    }

    void Apply(RE::TESObjectTREE* tree, const TreeConfig& cfg, float windStrength) {
        float scale = float(std::min(1.0, std::pow(windStrength, 2.0)));

        tree->data.trunkFlexibility = std::max(cfg.min.trunkFlexibility, cfg.max.trunkFlexibility * scale);
        tree->data.branchFlexibility = std::max(cfg.min.branchFlexibility, cfg.max.branchFlexibility * scale);

        tree->data.trunkAmplitude = std::max(cfg.min.trunkAmplitude, cfg.max.trunkAmplitude * scale);
        tree->data.leafAmplitude = std::max(cfg.min.leafAmplitude, cfg.max.leafAmplitude * scale);
        tree->data.frontAmplitude = std::max(cfg.min.frontAmplitude, cfg.max.frontAmplitude * scale);
        tree->data.backAmplitude = std::max(cfg.min.backAmplitude, cfg.max.backAmplitude * scale);
        tree->data.sideAmplitude = std::max(cfg.min.sideAmplitude, cfg.max.sideAmplitude * scale);

        tree->data.frontFrequency = std::max(cfg.min.frontFrequency, cfg.max.frontFrequency * scale);
        tree->data.backFrequency = std::max(cfg.min.backFrequency, cfg.max.backFrequency * scale);
        tree->data.sideFrequency = std::max(cfg.min.sideFrequency, cfg.max.sideFrequency * scale);
        tree->data.leafFlexibility = std::max(cfg.min.leafFlexibility, cfg.max.leafFlexibility * scale);
        tree->data.leafFrequency = std::max(cfg.min.leafFrequency, cfg.max.leafFrequency * scale);
    }

private:

    bool LoadConfig(const std::string& path, TreeConfig& outConfig) {
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
        
        auto form = RE::TESForm::LookupByID(formID);
        if (!form) {
            logger::error("Can't find form for FormID {} '{}'", j["FormID"].get<std::string>(), outConfig.formID);
            return false;
        }

        if (auto tree = form->As<RE::TESObjectTREE>()) {
            outConfig.formID = form->GetFormID();
            if (j.contains("trunkFlexibility")) outConfig.max.trunkFlexibility = j["trunkFlexibility"].get<float>();
            if (j.contains("branchFlexibility")) outConfig.max.branchFlexibility = j["branchFlexibility"].get<float>();

            if (j.contains("trunkAmplitude")) outConfig.max.trunkAmplitude = j["trunkAmplitude"].get<float>();
            if (j.contains("frontAmplitude")) outConfig.max.frontAmplitude = j["frontAmplitude"].get<float>();
            if (j.contains("backAmplitude")) outConfig.max.backAmplitude = j["backAmplitude"].get<float>();
            if (j.contains("sideAmplitude")) outConfig.max.sideAmplitude = j["sideAmplitude"].get<float>();

            if (j.contains("frontFrequency")) outConfig.max.frontFrequency = j["frontFrequency"].get<float>();
            if (j.contains("backFrequency")) outConfig.max.backFrequency = j["backFrequency"].get<float>();
            if (j.contains("sideFrequency")) outConfig.max.sideFrequency = j["sideFrequency"].get<float>();

            if (j.contains("leafFlexibility")) outConfig.max.leafFlexibility = j["leafFlexibility"].get<float>();
            if (j.contains("leafAmplitude")) outConfig.max.leafAmplitude = j["leafAmplitude"].get<float>();
            if (j.contains("leafFrequency")) outConfig.max.leafFrequency = j["leafFrequency"].get<float>();

            outConfig.min.trunkFlexibility = tree->data.trunkFlexibility;
            outConfig.min.branchFlexibility = tree->data.branchFlexibility;

            outConfig.min.trunkAmplitude = tree->data.trunkAmplitude;
            outConfig.min.frontAmplitude = tree->data.frontAmplitude;
            outConfig.min.backAmplitude = tree->data.backAmplitude;
            outConfig.min.sideAmplitude = tree->data.sideAmplitude;

            outConfig.min.frontFrequency = tree->data.frontFrequency;
            outConfig.min.backFrequency = tree->data.backFrequency;
            outConfig.min.sideFrequency = tree->data.sideFrequency;

            outConfig.min.leafFlexibility = tree->data.leafFlexibility;
            outConfig.min.leafAmplitude = tree->data.leafAmplitude;
            outConfig.min.leafFrequency = tree->data.leafFrequency;
            outConfig.filePath = path;
        } else {
            logger::error("FormID {} is not a tree", outConfig.formID);
            return false;
        }


        return true;
    }

    void SaveConfigToFile(const RE::FormID formID, const TreeDataConfig& cfg, std::string name = "") {
        std::filesystem::create_directories("Data\\SKSE\\Plugins\\DynamicWind\\Trees");

        std::string path = std::format("Data\\SKSE\\Plugins\\DynamicWind\\Trees\\{:08X}.json", formID);
        if (!name.empty()) {
            logger::info("Saving TreeConfig for {} ({:08X})", name, formID);
            path = std::format("Data\\SKSE\\Plugins\\DynamicWind\\Trees\\{}_{:08X}.json", name, formID);
        } else {
            logger::info("Saving TreeConfig for {:08X}", formID);
        }

        nlohmann::json j;
        j["FormID"] = Utils::FormIDToString(formID);

        j["trunkFlexibility"] = cfg.trunkFlexibility;
        j["branchFlexibility"] = cfg.branchFlexibility;

        j["trunkAmplitude"] = cfg.trunkAmplitude;

        j["frontAmplitude"] = cfg.frontAmplitude;
        j["backAmplitude"] = cfg.backAmplitude;
        j["sideAmplitude"] = cfg.sideAmplitude;

        j["frontFrequency"] = cfg.frontFrequency;
        j["backFrequency"] = cfg.backFrequency;
        j["sideFrequency"] = cfg.sideFrequency;

        j["leafFlexibility"] = cfg.leafFlexibility;
        j["leafAmplitude"] = cfg.leafAmplitude;
        j["leafFrequency"] = cfg.leafFrequency;

        std::ofstream file(path);
        file << j.dump(4);
    }

    std::vector<TreeConfig> LoadAllConfigs(const std::string& folder) {
        std::vector<TreeConfig> configs;

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

            TreeConfig cfg;
            if (LoadConfig(entry.path().string(), cfg)) {
                configs.push_back(cfg);
            }
        }
        return configs;
    }

    std::unordered_map<RE::FormID, TreeConfig> _configs;
};