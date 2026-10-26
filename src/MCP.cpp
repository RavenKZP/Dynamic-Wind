#include "MCP.h"

#include "WindManager.h"
#include "Settings.h"
#include "SKSEMCP/SKSEMenuFramework.hpp"

namespace MCP {

    void Register() {
        if (!SKSEMenuFramework::IsInstalled()) {
            logger::warn("SKSE Menu Framework is not installed. Cannot register menu.");
            return;
        }
        SKSEMenuFramework::SetSection("Dynamic Wind");
        SKSEMenuFramework::AddSectionItem("Settings", RenderConfig);
        SKSEMenuFramework::AddSectionItem("Framework Options", RenderFrameworkTools);
        SKSEMenuFramework::AddSectionItem("Loaded Configuration", RenderLoadedConfigs);
#ifndef NDEBUG
        SKSEMenuFramework::AddSectionItem("Log", RenderLog);
#endif

        logger::info("SKSE Menu Framework registered.");
    }

    void __stdcall RenderConfig() { auto* set = Config::GetSingleton();
        if (ImGuiMCP::Checkbox("Mod Active", &set->ModActive)) {
            if (!set->ModActive) {
                Wind::Manager::GetSingleton()->DisableMod();
            } else {
                Wind::Manager::GetSingleton()->EnableMod();
            }
        }
        ImGuiMCP::Checkbox("Animation Handler", &set->AnimationHandlerEnabled);
        ImGuiMCP::Checkbox("Base Object Swap Handler", &set->BaseObjSwapHandlerEnabled);
        ImGuiMCP::Checkbox("Model Swap Handler", &set->ModelSwapHandlerEnabled);
        ImGuiMCP::Checkbox("Push Handler", &set->PushHandlerEnabled);
        ImGuiMCP::Checkbox("Rotation Handler", &set->RotationHandlerEnabled);
        if (ImGuiMCP::Checkbox("Tree Handler", &set->TreeHandlerEnabled)) {
            if (!set->TreeHandlerEnabled) {
                Wind::Manager::GetSingleton()->DisableMod();
            } else {
                Wind::Manager::GetSingleton()->EnableMod();
            }
        }
        ImGuiMCP::Checkbox("Visibility Handler", &set->VisibilityHandlerEnabled);
        ImGuiMCP::SliderFloat("Min Wind Strength", &set->minWindStrength, 0.0f, 1.0f);
        ImGuiMCP::SliderFloat("Max Wind Strength", &set->maxWindStrength, 0.0f, 1.0f);
        if (set->maxWindStrength < set->minWindStrength) {
            set->maxWindStrength = set->minWindStrength;
        }
        if (ImGuiMCP::Button("Save Settings")) {
            set->SaveIni();
        }
    }

    static const char* GetCompassLabel(float windAngle) {
        static const char* labels[8] = {"E", "NE", "N", "NW", "W", "SW", "S", "SE"};

        float degrees = windAngle * (180.0f / std::numbers::pi_v<float>);
        if (degrees < 0) degrees += 360.0f;

        int idx = static_cast<int>((degrees + 22.5f) / 45.0f) % 8;
        return labels[idx];
    }

    void __stdcall RenderFrameworkTools() {
        static float lifetime = 10.0f;

        auto* sky = RE::Sky::GetSingleton();

        if (sky) {
            ImGuiMCP::Text("Wind Data");
            auto isRaining = sky->IsRaining();
            auto isSnowing = sky->IsSnowing();

            ImGuiMCP::Text("Is Raining: %s", isRaining ? "Yes" : "No");
            ImGuiMCP::Text("Is Snowing: %s", isSnowing ? "Yes" : "No");
            auto* windManager = Wind::Manager::GetSingleton();

            if (ImGuiMCP::CollapsingHeader("Wind Manager", ImGuiMCP::ImGuiTreeNodeFlags_DefaultOpen)) {
                static bool enableTestMode = false;
                if (ImGuiMCP ::Checkbox("Test Mode", &enableTestMode)) {
                    windManager->SetTestMode(enableTestMode);
                }
                ImGuiMCP::SameLine();
                static bool updateFramework = true;
                if (ImGuiMCP::Checkbox("Update Framework", &updateFramework)) {
                    windManager->SetUpdateFramework(updateFramework);
                }
                auto [windAngle, windSpeed] = windManager->GetTargets();
                ImGuiMCP::Text("Wind Direction Target: %.1f", windAngle);
                ImGuiMCP::SameLine();
                ImGuiMCP::Text(" (%s)", GetCompassLabel(windAngle));
                ImGuiMCP::Text("Wind Speed Target: %.1f", windSpeed);

                ImGuiMCP::SliderFloat("Sky: Wind Direction", &sky->windAngle, -M_PI, M_PI);
                ImGuiMCP::SameLine();
                ImGuiMCP::Text(" (%s)", GetCompassLabel(sky->windAngle));
                ImGuiMCP::SliderFloat("Sky: Wind Speed", &sky->windSpeed, 0.0f, 1.0f);
                static float newAngle{0.0f}, newSpeed{0.0f};
                ImGuiMCP::SliderFloat("New Target Direction", &newAngle, -M_PI, M_PI);
                ImGuiMCP::SameLine();
                ImGuiMCP::Text(" (%s)", GetCompassLabel(newAngle));
                ImGuiMCP::SliderFloat("New Target Speed", &newSpeed, 0.0f, 1.0f);
                if (ImGuiMCP::Button("Generate New Targets")) {
                    windManager->EnableMod();
                }
                ImGuiMCP::SameLine();
                if (ImGuiMCP::Button("Applay New Targets")) {
                    windManager->SetTargets(newAngle, newSpeed);
                }
                ImGuiMCP::SameLine();
                if (ImGuiMCP::Button("Refresh All refs")) {
                    RE::TES::GetSingleton()->ForEachReference([](RE::TESObjectREFR* ref) {
                        auto [angle, strength] = Wind::Manager::GetSingleton()->GetTargets();
                        WindFramework::GetSingleton()->RefLoad(ref, angle, strength);
                        return RE::BSContainer::ForEachResult::kContinue;
                    });
                }
                auto* treeMgr = RE::BSTreeManager::GetSingleton();
                if (treeMgr) {
                    ImGuiMCP::Text("TreeWind x: %.2f", treeMgr->windDirection.x);
                    ImGuiMCP::Text("TreeWind y: %.2f", treeMgr->windDirection.y);
                    float treeDir = std::atan2(treeMgr->windDirection.x, treeMgr->windDirection.y);
                    if (ImGuiMCP::SliderFloat("TreeManager: Wind Direction", &treeDir, -M_PI, M_PI)) {
                        treeMgr->windDirection.x = std::sin(treeDir);
                        treeMgr->windDirection.y = std::cos(treeDir);
                    }
                    ImGuiMCP::SliderFloat("TreeManager: Wind Magnitude", &treeMgr->windMagnitude, 0.0f, 10.0f);
                }
            }
        }

        if (auto refPtr = RE::Console::GetSelectedRef()) {
            if (auto ref = refPtr.get()) {
                if (auto* base = ref->GetBaseObject()) {
                    auto windFram = WindFramework::GetSingleton();
                    // Try casting to tree (TESObjectTREE)
                    if (auto* tree = base->As<RE::TESObjectTREE>()) {
                        // Access tree data (CNAM)
                        auto& data = tree->data;

                        if (ImGuiMCP::CollapsingHeader("Tree Wind Settings")) {

                            ImGuiMCP::SliderFloat("Trunk Flexibility", &data.trunkFlexibility, 0.0f, 10.0f);
                            ImGuiMCP::SliderFloat("Trunk Amplitude", &data.trunkAmplitude, 0.0f, 10.0f);
                            ImGuiMCP::SliderFloat("Branch Flexibility", &data.branchFlexibility, 0.0f, 10.0f);

                            ImGuiMCP::Separator();

                            ImGuiMCP::SliderFloat("Front Amplitude", &data.frontAmplitude, 0.0f, 10.0f);
                            ImGuiMCP::SliderFloat("Front Frequency", &data.frontFrequency, 0.0f, 10.0f);

                            ImGuiMCP::Separator();

                            ImGuiMCP::SliderFloat("Back Amplitude", &data.backAmplitude, 0.0f, 10.0f);
                            ImGuiMCP::SliderFloat("Back Frequency", &data.backFrequency, 0.0f, 10.0f);

                            ImGuiMCP::Separator();

                            ImGuiMCP::SliderFloat("Side Amplitude", &data.sideAmplitude, 0.0f, 10.0f);
                            ImGuiMCP::SliderFloat("Side Frequency", &data.sideFrequency, 0.0f, 10.0f);

                            ImGuiMCP::Separator();

                            ImGuiMCP::SliderFloat("Leaf Flexibility", &data.leafFlexibility, 0.0f, 10.0f);
                            ImGuiMCP::SliderFloat("Leaf Amplitude", &data.leafAmplitude, 0.0f, 10.0f);
                            ImGuiMCP::SliderFloat("Leaf Frequency", &data.leafFrequency, 0.0f, 10.0f);

                            if (ImGuiMCP::Button("Applay Leaf Settings")) {
                                ref->Disable();
                                ref->Enable(false);
                            }
                            ImGuiMCP::SameLine();
                            if (ImGuiMCP::Button("Save Tree Config")) {
                                TreeDataConfig cfg;

                                cfg.trunkFlexibility = data.trunkFlexibility;
                                cfg.branchFlexibility = data.branchFlexibility;

                                cfg.trunkAmplitude = data.trunkAmplitude;

                                cfg.frontAmplitude = data.frontAmplitude;
                                cfg.backAmplitude = data.backAmplitude;
                                cfg.sideAmplitude = data.sideAmplitude;

                                cfg.frontFrequency = data.frontFrequency;
                                cfg.backFrequency = data.backFrequency;
                                cfg.sideFrequency = data.sideFrequency;

                                cfg.leafFlexibility = data.leafFlexibility;
                                cfg.leafAmplitude = data.leafAmplitude;
                                cfg.leafFrequency = data.leafFrequency;

                                windFram->AddNewTreeConfig(tree, cfg);

                                logger::info("Saved TreeConfig for {:08X}", tree->GetFormID());
                            }
                            ImGuiMCP::SameLine();
                            if (ImGuiMCP::Button("Reset Tree Config")) {
                                TreeConfig cfg = windFram->GetTreeConfig(tree->GetFormID());
                                data.trunkFlexibility = cfg.min.trunkFlexibility;
                                data.branchFlexibility = cfg.min.branchFlexibility;

                                data.trunkAmplitude = cfg.min.trunkAmplitude;

                                data.frontAmplitude = cfg.min.frontAmplitude;
                                data.backAmplitude = cfg.min.backAmplitude;
                                data.sideAmplitude = cfg.min.sideAmplitude;

                                data.frontFrequency = cfg.min.frontFrequency;
                                data.backFrequency = cfg.min.backFrequency;
                                data.sideFrequency = cfg.min.sideFrequency;

                                data.leafFlexibility = cfg.min.leafFlexibility;
                                data.leafAmplitude = cfg.min.leafAmplitude;
                                data.leafFrequency = cfg.min.leafFrequency;
                            }
                            if (ImGuiMCP::Button("Remove Tree Config")) {
                                windFram->RemoveTreeConfig(tree->GetFormID());
                            }
                        }
                    }

                    if (ImGuiMCP::CollapsingHeader("Animation Settings")) {
                        static float minAnimSpeed = 0.0f;
                        static float maxAnimSpeed = 1.0f;
                        static float headingRotation = 0.0f;
                        static float angleFactor = 0.0f;
                        ImGuiMCP::SliderFloat("Min Animation Speed", &minAnimSpeed, -10.0f, 20.0f);
                        ImGuiMCP::InputFloat("MinAS:", &minAnimSpeed);
                        ImGuiMCP::SliderFloat("Max Animation Speed", &maxAnimSpeed, -10.0f, 20.0f);
                        ImGuiMCP::InputFloat("MaxAS:", &maxAnimSpeed);

                        if (minAnimSpeed > maxAnimSpeed) maxAnimSpeed = minAnimSpeed;
                        if (ImGuiMCP::Button("Applay min speed")) {
                            Utils::ApplySpeedToNode(ref->Get3D(), minAnimSpeed);
                        }
                        ImGuiMCP::SameLine();
                        if (ImGuiMCP::Button("Applay max speed")) {
                            Utils::ApplySpeedToNode(ref->Get3D(), maxAnimSpeed);
                        }

                        ImGuiMCP::SliderFloat("Global Heading Rotation", &headingRotation, -M_PI, M_PI, "%.2f");
                        ImGuiMCP::SliderFloat("Global Angle Factor", &angleFactor, 0.0f, 1.0f, "%.2f");
                        if (ImGuiMCP::Button("Save Animation Config (Base)")) {
                                windFram->AddNewAnimationConfig(base, minAnimSpeed, maxAnimSpeed, headingRotation,
                                                                angleFactor);
                            
                            RE::TES::GetSingleton()->ForEachReference([](RE::TESObjectREFR* ref) {
                                    auto [angle, strength] = Wind::Manager::GetSingleton()->GetTargets();
                                    WindFramework::GetSingleton()->RefLoad(ref, angle, strength);
                                    return RE::BSContainer::ForEachResult::kContinue;
                                });
                        }
                        ImGuiMCP::SameLine();
                        if (ImGuiMCP::Button("Save Animation Config (Ref)")) {
                                windFram->AddNewAnimationConfig(ref, minAnimSpeed, maxAnimSpeed, headingRotation,
                                                                angleFactor);
                            
                            RE::TES::GetSingleton()->ForEachReference([](RE::TESObjectREFR* ref) {
                                    auto [angle, strength] = Wind::Manager::GetSingleton()->GetTargets();
                                    WindFramework::GetSingleton()->RefLoad(ref, angle, strength);
                                    return RE::BSContainer::ForEachResult::kContinue;
                                });
                        }
                        ImGuiMCP::SameLine();
                        if (ImGuiMCP::Button("Remove Animation Config (Base)")) {
                            windFram->RemoveAnimationConfig(base->GetFormID());
                        }
                        ImGuiMCP::SameLine();
                        if (ImGuiMCP::Button("Remove Animation Config (Ref)")) {
                            windFram->RemoveAnimationConfig(ref->GetFormID());
                        }
                    }

                    if (ImGuiMCP::CollapsingHeader("Rotation Settings")) {
                        static float headingRotation = 0.0f;
                        static float Pi = 0;
                        if (ImGuiMCP::SliderFloat("Heading Rotation", &headingRotation, -M_PI, M_PI)) {
                            Pi = headingRotation / M_PI;
                        }
                        ImGuiMCP::InputFloat("HR:", &headingRotation);

                        if (ImGuiMCP::SliderFloat("Pi", &Pi, -1.0f, 1.0f), "%.2f") {
                            headingRotation = Pi * M_PI;
                        }
                        ImGuiMCP::InputFloat("Pi:", &Pi);

                        static float allowedAngle = 0.0f;
                        if (ImGuiMCP::Button("Use current Angle")) {
                            allowedAngle = ref->GetAngleZ() * (180.0f / M_PI);
                        }
                        ImGuiMCP::SliderFloat("Allowed Angle", &allowedAngle, 0.0f, 360.0f);
                        ImGuiMCP::InputFloat("AA:", &allowedAngle);
                        static std::vector<float> allowedAngles;
                        if (ImGuiMCP::Button("Add Allowed Angle")) {
                            allowedAngles.push_back(allowedAngle);
                        }
                        for (size_t i = 0; i < allowedAngles.size(); i++) {
                            ImGuiMCP::Text("Allowed Angle:");
                            ImGuiMCP::InputFloat(("##AAList" + std::to_string(i)).c_str(), &allowedAngles[i]);
                            ImGuiMCP::SameLine();
                            if (ImGuiMCP::Button(("Remove##" + std::to_string(i)).c_str())) {
                                allowedAngles.erase(allowedAngles.begin() + i);
                                break;
                            }
                        }

                        if (ImGuiMCP::Button("Save Rotation Config (Base)")) {
                            windFram->AddNewRotationConfig(base, headingRotation, allowedAngles);
                            RE::TES::GetSingleton()->ForEachReference([](RE::TESObjectREFR* ref) {
                                auto [angle, strength] = Wind::Manager::GetSingleton()->GetTargets();
                                WindFramework::GetSingleton()->RefLoad(ref, angle, strength);
                                return RE::BSContainer::ForEachResult::kContinue;
                            });
                        }
                        ImGuiMCP::SameLine();
                        if (ImGuiMCP::Button("Save Rotation Config (Ref)")) {
                            windFram->AddNewRotationConfig(ref, headingRotation, allowedAngles);
                            RE::TES::GetSingleton()->ForEachReference([](RE::TESObjectREFR* ref) {
                                auto [angle, strength] = Wind::Manager::GetSingleton()->GetTargets();
                                WindFramework::GetSingleton()->RefLoad(ref, angle, strength);
                                return RE::BSContainer::ForEachResult::kContinue;
                            });
                        }
                        ImGuiMCP::SameLine();
                        if (ImGuiMCP::Button("Remove Rotation Config (Base)")) {
                            windFram->RemoveRotationConfig(base->GetFormID());
                        }
                        ImGuiMCP::SameLine();
                        if (ImGuiMCP::Button("Remove Rotation Config (Ref)")) {
                            windFram->RemoveRotationConfig(ref->GetFormID());
                        }
                    }

                    if (ImGuiMCP::CollapsingHeader("Visibility Settings")) {
                        static float minVisibility = 0.0f;
                        ImGuiMCP::SliderFloat("Min Visibility", &minVisibility, 0.0f, 1.0f);
                        static float maxVisibility = 1.0f;
                        ImGuiMCP::SliderFloat("Max Visibility", &maxVisibility, 0.0f, 1.0f);
                        if (minVisibility > maxVisibility) maxVisibility = minVisibility;
                        static float minWindStrength = 0.0f;
                        ImGuiMCP::SliderFloat("Min Wind Strength", &minWindStrength, 0.0f, 1.0f);
                        static float maxWindStrength = 1.0f;
                        ImGuiMCP::SliderFloat("Max Wind Strength", &maxWindStrength, 0.0f, 1.0f);
                        static float headingRotation = 0.0f;
                        ImGuiMCP::SliderFloat("Global Heading Rotation", &headingRotation, -M_PI, M_PI, "%.2f");
                        static float angleFactor = 0.0f;
                        ImGuiMCP::SliderFloat("Global Angle Factor", &angleFactor, 0.0f, 1.0f, "%.2f");

                        if (ImGuiMCP::Button("Save Visibility Config (Base)")) {
                            windFram->AddNewVisibilityConfig(base, minVisibility, maxVisibility, minWindStrength,
                                                             maxWindStrength, headingRotation, angleFactor);
                            RE::TES::GetSingleton()->ForEachReference([](RE::TESObjectREFR* ref) {
                                auto [angle, strength] = Wind::Manager::GetSingleton()->GetTargets();
                                WindFramework::GetSingleton()->RefLoad(ref, angle, strength);
                                return RE::BSContainer::ForEachResult::kContinue;
                            });
                        }
                        ImGuiMCP::SameLine();
                        if (ImGuiMCP::Button("Save Visibility Config (Ref)")) {
                            windFram->AddNewVisibilityConfig(ref, minVisibility, maxVisibility, minWindStrength,
                                                             maxWindStrength, headingRotation, angleFactor);
                            RE::TES::GetSingleton()->ForEachReference([](RE::TESObjectREFR* ref) {
                                auto [angle, strength] = Wind::Manager::GetSingleton()->GetTargets();
                                WindFramework::GetSingleton()->RefLoad(ref, angle, strength);
                                return RE::BSContainer::ForEachResult::kContinue;
                            });
                        }
                        ImGuiMCP::SameLine();
                        if (ImGuiMCP::Button("Remove Visibility Config (Base)")) {
                            windFram->RemoveVisibilityConfig(base->GetFormID());
                        }
                        ImGuiMCP::SameLine();
                        if (ImGuiMCP::Button("Remove Visibility Config (Ref)")) {
                            windFram->RemoveVisibilityConfig(ref->GetFormID());
                        }
                    }

                    if (ImGuiMCP::CollapsingHeader("Push Settings")) {
                        static float windSensitivity{1.0f};
                        ImGuiMCP::SliderFloat("Wind Sensitivity", &windSensitivity, 0.0f, 5.0f);
                        ImGuiMCP::SameLine();
                        ImGuiMCP::InputFloat("WS:", &windSensitivity);
                        if (ImGuiMCP::Button("Save Push Config (Base)")) {
                            windFram->AddNewPushConfig(base, windSensitivity);
                            RE::TES::GetSingleton()->ForEachReference([](RE::TESObjectREFR* ref) {
                                auto [angle, strength] = Wind::Manager::GetSingleton()->GetTargets();
                                WindFramework::GetSingleton()->RefLoad(ref, angle, strength);
                                return RE::BSContainer::ForEachResult::kContinue;
                            });
                        }
                        ImGuiMCP::SameLine();
                        if (ImGuiMCP::Button("Save Push Config (Ref)")) {
                            windFram->AddNewPushConfig(ref, windSensitivity);
                            RE::TES::GetSingleton()->ForEachReference([](RE::TESObjectREFR* ref) {
                                auto [angle, strength] = Wind::Manager::GetSingleton()->GetTargets();
                                WindFramework::GetSingleton()->RefLoad(ref, angle, strength);
                                return RE::BSContainer::ForEachResult::kContinue;
                            });
                        }
                        ImGuiMCP::SameLine();
                        if (ImGuiMCP::Button("Remove Push Config (Base)")) {
                            windFram->RemovePushConfig(base->GetFormID());
                        }
                        ImGuiMCP::SameLine();
                        if (ImGuiMCP::Button("Remove Push Config (Ref)")) {
                            windFram->RemovePushConfig(ref->GetFormID());
                        }
                    }

                    if (ImGuiMCP::CollapsingHeader("Model Replace Config")) {
                        static std::vector<ModelSwapEntry> swaps;
                        static char modelPath[256] = "";
                        static float strength = 1.0f;
                        static float headingRotation = 0.0f;
                        static float angleFactor = 0.0f;
                        ImGuiMCP::Text("IMPORTANT: this module is experimental and maybe unstable!");
                        ImGuiMCP::Text("The swapped meshes can behave in unpredicted way (lack of animation)");
                        ImGuiMCP::Text("Use it wisely! Please provide feedback to mod author.");
                        ImGuiMCP::Text("");
                        ImGuiMCP::SliderFloat("Global Heading Rotation", &headingRotation, -M_PI, M_PI, "%.2f");
                        ImGuiMCP::SliderFloat("Global Angle Factor", &angleFactor, 0.0f, 1.0f, "%.2f");
                        ImGuiMCP::SliderFloat("Global Strength", &strength, 0.0f, 1.0f, "%.2f");

                        if (ImGuiMCP::Button("Add Entry")) {
                            std::string currentModel = Utils::GetModelPath(base);
                            strncpy_s(modelPath, currentModel.c_str(), sizeof(modelPath) - 1);
                            swaps.push_back({strength, modelPath});
                        }

                        static std::vector<std::string> modelBuffers;

                        if (modelBuffers.size() != swaps.size()) {
                            modelBuffers.resize(swaps.size());
                            for (size_t i = 0; i < swaps.size(); ++i)
                                modelBuffers[i] = swaps[i].modelPath;
                        }

                        for (size_t i = 0; i < swaps.size(); ++i) {
                            ImGuiMCP::InputFloat(std::format("Strength##{}", i).c_str(), &swaps[i].strength, 0.01f, 1.0f, "%.2f");

                            char buf[256];
                            strncpy_s(buf, modelBuffers[i].c_str(), sizeof(buf) - 1);
                            if (ImGuiMCP::InputText(std::format("Model##{}", i).c_str(), buf, sizeof(buf))) {
                                modelBuffers[i] = buf;
                                swaps[i].modelPath = buf;
                            }
                            ImGuiMCP::SameLine();
                            if (ImGuiMCP::Button(std::format("Remove##{}", i).c_str())) {
                                swaps.erase(swaps.begin() + i);
                                modelBuffers.erase(modelBuffers.begin() + i);
                                break;
                            }
                        }

                        if (ImGuiMCP::Button("Save ModelSwap Config (Base)")) {
                            windFram->AddNewModelSwapConfig(base, headingRotation, angleFactor, swaps);
                            RE::TES::GetSingleton()->ForEachReference([](RE::TESObjectREFR* ref) {
                                auto [loc_angle, loc_strength] = Wind::Manager::GetSingleton()->GetTargets();
                                WindFramework::GetSingleton()->RefLoad(ref, loc_angle, loc_strength);
                                return RE::BSContainer::ForEachResult::kContinue;
                            });
                        }
                        ImGuiMCP::SameLine();
                        if (ImGuiMCP::Button("Save ModelSwap Config (Ref)")) {
                            windFram->AddNewModelSwapConfig(ref, headingRotation, angleFactor, swaps);
                            RE::TES::GetSingleton()->ForEachReference([](RE::TESObjectREFR* ref) {
                                auto [loc_angle, loc_strength] = Wind::Manager::GetSingleton()->GetTargets();
                                WindFramework::GetSingleton()->RefLoad(ref, loc_angle, loc_strength);
                                return RE::BSContainer::ForEachResult::kContinue;
                            });
                        }
                        ImGuiMCP::SameLine();
                        if (ImGuiMCP::Button("Clear List")) {
                            swaps.clear();
                        }
                        ImGuiMCP::SameLine();
                        if (ImGuiMCP::Button("Remove ModelSwap Config (Base)")) {
                            windFram->RemoveModelSwapConfig(base->GetFormID());
                        }
                        ImGuiMCP::SameLine();
                        if (ImGuiMCP::Button("Remove ModelSwap Config (Ref)")) {
                            windFram->RemoveModelSwapConfig(ref->GetFormID());
                        }
                    }

                    if (ImGuiMCP::CollapsingHeader("Base Object Replace Config")) {
                        static std::vector<BaseObjSwapEntry> swaps;
                        static float strength = 1.0f;
                        static float headingRotation = 0.0f;
                        static float angleFactor = 0.0f;
                        auto formIDStr = Utils::FormIDToString(base->GetFormID());

                        ImGuiMCP::SliderFloat("Global Heading Rotation", &headingRotation, -M_PI, M_PI, "%.2f");
                        ImGuiMCP::SliderFloat("Global Angle Factor", &angleFactor, 0.0f, 1.0f, "%.2f");
                        ImGuiMCP::SliderFloat("Global Strength", &strength, 0.0f, 1.0f, "%.2f");
                        ImGuiMCP::Text(formIDStr.c_str());

                        ImGuiMCP::SameLine();
                        if (ImGuiMCP::Button("Add Entry")) {
                            swaps.push_back({strength, base->GetFormID()});
                        }

                        static std::vector<std::string> formBuffers;
                        if (formBuffers.size() != swaps.size()) {
                            formBuffers.resize(swaps.size());
                            for (size_t i = 0; i < swaps.size(); ++i)
                                formBuffers[i] = Utils::FormIDToString(swaps[i].formID);
                        }

                        for (size_t i = 0; i < swaps.size(); ++i) {
                            ImGuiMCP::InputFloat(std::format("Strength##{}", i).c_str(), &swaps[i].strength, 0.01f, 1.0f, "%.2f");
                            char buf[64];
                            strncpy_s(buf, formBuffers[i].c_str(), sizeof(buf) - 1);
                            if (ImGuiMCP::InputText(std::format("FormID##{}", i).c_str(), buf, sizeof(buf))) {
                                formBuffers[i] = buf;
                                if (auto formID = Utils::ParseForm(buf)) {
                                    swaps[i].formID = formID;
                                }
                            }
                            ImGuiMCP::SameLine();
                            if (ImGuiMCP::Button(std::format("Remove##{}", i).c_str())) {
                                swaps.erase(swaps.begin() + i);
                                formBuffers.erase(formBuffers.begin() + i);
                                break;
                            }
                        }

                        if (ImGuiMCP::Button("Clear List")) {
                            swaps.clear();
                            formBuffers.clear();
                        }
                        ImGuiMCP::SameLine();
                        if (ImGuiMCP::Button("Save BaseObjSwap Config (Base)")) {
                            windFram->AddNewBaseObjSwapConfig(base->GetFormID(), swaps, headingRotation, angleFactor);
                            RE::TES::GetSingleton()->ForEachReference([](RE::TESObjectREFR* ref) {
                                auto [loc_angle, loc_strength] = Wind::Manager::GetSingleton()->GetTargets();
                                WindFramework::GetSingleton()->RefLoad(ref, loc_angle, loc_strength);
                                return RE::BSContainer::ForEachResult::kContinue;
                            });
                        }
                        ImGuiMCP::SameLine();
                        if (ImGuiMCP::Button("Save BaseObjSwap Config (Ref)")) {
                            windFram->AddNewBaseObjSwapConfig(ref->GetFormID(), swaps, headingRotation, angleFactor);
                            RE::TES::GetSingleton()->ForEachReference([](RE::TESObjectREFR* ref) {
                                auto [loc_angle, loc_strength] = Wind::Manager::GetSingleton()->GetTargets();
                                WindFramework::GetSingleton()->RefLoad(ref, loc_angle, loc_strength);
                                return RE::BSContainer::ForEachResult::kContinue;
                            });
                        }
                        ImGuiMCP::SameLine();
                        if (ImGuiMCP::Button("Remove BaseObjSwap Config")) {
                            windFram->RemoveBaseObjSwapConfig(base->GetFormID());
                        }
                    }


                    if (ImGuiMCP::Button("Remove from Wind Framework")) {
                        windFram->RemoveVisibilityConfig(base->GetFormID());
                        windFram->RemoveVisibilityConfig(ref->GetFormID());

                        windFram->RemoveRotationConfig(base->GetFormID());
                        windFram->RemoveRotationConfig(ref->GetFormID());

                        windFram->RemoveAnimationConfig(base->GetFormID());
                        windFram->RemoveAnimationConfig(ref->GetFormID());

                        windFram->RemoveTreeConfig(base->GetFormID());

                        windFram->RemovePushConfig(base->GetFormID());
                        windFram->RemovePushConfig(ref->GetFormID());

                        windFram->RemoveModelSwapConfig(base->GetFormID());
                        windFram->RemoveModelSwapConfig(ref->GetFormID());

                        windFram->RemoveBaseObjSwapConfig(base->GetFormID());
                        windFram->RemoveBaseObjSwapConfig(ref->GetFormID());
                    }
                }
            }
        }
    }

    void __stdcall RenderLoadedConfigs() {
        auto* windFram = WindFramework::GetSingleton();
        auto configs = windFram->GetConfigs();

        std::vector<RE::FormID> sortedFormIDs;
        for (const auto& [formID, _] : configs) {
            sortedFormIDs.push_back(formID);
        }
        std::sort(sortedFormIDs.begin(), sortedFormIDs.end());

        static std::unordered_map<RE::FormID, AnimationConfig> animEditBuf;
        static std::unordered_map<RE::FormID, RotationConfig> rotEditBuf;
        static std::unordered_map<RE::FormID, VisibilityConfig> visEditBuf;
        static std::unordered_map<RE::FormID, PushConfig> pushEditBuf;
        static std::unordered_map<RE::FormID, TreeDataConfig> treeEditBuf;
        static std::unordered_map<RE::FormID, ModelSwapConfig> modelEditBuf;
        static std::unordered_map<RE::FormID, BaseObjSwapConfig> baseEditBuf;

        static char filterBuf[64] = "";
        static bool showAnimConf = true;
        static bool showRotConf = true;
        static bool showVisConf = true;
        static bool showPushConf = true;
        static bool showTreeConf = true;
        static bool showModelConf = true;
        static bool showBaseConf = true;

        ImGuiMCP::Checkbox("Animation", &showAnimConf);
        ImGuiMCP::SameLine();
        ImGuiMCP::Checkbox("Rotation", &showRotConf);
        ImGuiMCP::SameLine();
        ImGuiMCP::Checkbox("Visibility", &showVisConf);
        ImGuiMCP::SameLine();
        ImGuiMCP::Checkbox("Push", &showPushConf);
        ImGuiMCP::SameLine();
        ImGuiMCP::Checkbox("Tree", &showTreeConf);
        ImGuiMCP::SameLine();
        ImGuiMCP::Checkbox("Model Swap", &showModelConf);
        ImGuiMCP::SameLine();
        ImGuiMCP::Checkbox("BaseObj Swap", &showBaseConf);

        ImGuiMCP::InputText("Filter (FormID / hex)", filterBuf, sizeof(filterBuf));

        for (auto formID : sortedFormIDs) {
            std::string formStr = std::format("{:08X}", formID);
            if (strlen(filterBuf) > 0 && formStr.find(filterBuf) == std::string::npos) {
                continue;
            }
            auto& config = configs.at(formID);
            ImGuiMCP::Separator();
            (ImGuiMCP::Text(std::format("FormID: {:08X}", formID, formID).c_str()));
            auto form = RE::TESForm::LookupByID(formID);
            if (form) {
                ImGuiMCP::SameLine();
                ImGuiMCP::Text("(%s)", form->GetName());
                ImGuiMCP::SameLine();
                ImGuiMCP::Text("(%s)", form->GetFormEditorID());
                ImGuiMCP::SameLine();
                ImGuiMCP::Text("(%s)", form->GetObjectTypeName());
            }
            // AnimationConfig
            if (config.animationConfig && showAnimConf) {
                if (!animEditBuf.contains(formID) || animEditBuf[formID].formID != formID) {
                    animEditBuf[formID] = *config.animationConfig;
                }
                auto& editAnim = animEditBuf[formID];
                if (editAnim.formID != formID) editAnim = *config.animationConfig;
                if (ImGuiMCP::CollapsingHeader(std::format("AnimationConfig##anim_{}", formID).c_str())) {
                    ImGuiMCP::InputFloat(std::format("Speed Min##anim_{}", formID).c_str(), &editAnim.speedMin);
                    ImGuiMCP::InputFloat(std::format("Speed Max##anim_{}", formID).c_str(), &editAnim.speedMax);
                    ImGuiMCP::InputFloat(std::format("Heading Rotation##anim_{}", formID).c_str(),
                                         &editAnim.headingRotation);
                    ImGuiMCP::InputFloat(std::format("Angle Factor##anim_{}", formID).c_str(), &editAnim.angleFactor);

                    if (ImGuiMCP::Button(std::format("Save##anim_{:08X}", formID).c_str())) {
                        windFram->AddNewAnimationConfig(editAnim.formID, editAnim.speedMin, editAnim.speedMax,
                                                        editAnim.headingRotation, editAnim.angleFactor);
                    }
                    ImGuiMCP::SameLine();
                    if (ImGuiMCP::Button(std::format("Remove##anim_{:08X}", formID).c_str())) {
                        windFram->RemoveAnimationConfig(formID);
                        animEditBuf.erase(formID);
                    }
                }
            }

            // RotationConfig
            if (config.rotationConfig && showRotConf) {
                if (!rotEditBuf.contains(formID) || rotEditBuf[formID].formID != formID) {
                    rotEditBuf[formID] = *config.rotationConfig;
                }
                auto& editRot = rotEditBuf[formID];
                if (editRot.formID != formID) editRot = *config.rotationConfig;
                if (ImGuiMCP::CollapsingHeader(std::format("RotationConfig##rot_{}", formID).c_str())) {
                    ImGuiMCP::InputFloat(std::format("Heading Rotation##rot_{}", formID).c_str(),
                                         &editRot.headingRotation);
                    if (editRot.allowedAngles) {
                        auto& angles = *editRot.allowedAngles;

                        for (size_t i = 0; i < angles.size(); ++i) {
                            ImGuiMCP::PushID(static_cast<int>(i));

                            ImGuiMCP::InputFloat("##angle", &angles[i]);

                            ImGuiMCP::SameLine();
                            if (ImGuiMCP::Button("X")) {
                                angles.erase(angles.begin() + i);
                                ImGuiMCP::PopID();
                                break;
                            }

                            ImGuiMCP::PopID();
                        }

                        if (ImGuiMCP::Button(std::format("+ Add Angle##{}", formID).c_str())) {
                            angles.push_back(0.0f);
                        }
                    }
                    if (ImGuiMCP::Button(std::format("Save##rot_{:08X}", formID).c_str())) {
                        windFram->AddNewRotationConfig(editRot.formID, editRot.headingRotation, editRot.allowedAngles);
                    }
                    ImGuiMCP::SameLine();
                    if (ImGuiMCP::Button(std::format("Remove##rot_{:08X}", formID).c_str())) {
                        windFram->RemoveRotationConfig(formID);
                        rotEditBuf.erase(formID);
                    }
                }
            }

            // VisibilityConfig
            if (config.visibilityConfig && showVisConf) {
                if (!visEditBuf.contains(formID) || visEditBuf[formID].formID != formID) {
                    visEditBuf[formID] = *config.visibilityConfig;
                }
                auto& editVis = visEditBuf[formID];
                if (editVis.formID != formID) editVis = *config.visibilityConfig;
                if (ImGuiMCP::CollapsingHeader(std::format("VisibilityConfig##vis_{}", formID).c_str())) {
                    ImGuiMCP::InputFloat(std::format("Min Visibility##vis_{}", formID).c_str(), &editVis.minVisibility);
                    ImGuiMCP::InputFloat(std::format("Max Visibility##vis_{}", formID).c_str(), &editVis.maxVisibility);
                    ImGuiMCP::InputFloat(std::format("Min Wind Strength##vis_{}", formID).c_str(),
                                         &editVis.minWindStrength);
                    ImGuiMCP::InputFloat(std::format("Max Wind Strength##vis_{}", formID).c_str(),
                                         &editVis.maxWindStrength);
                    ImGuiMCP::InputFloat(std::format("Heading Rotation##vis_{}", formID).c_str(),
                                         &editVis.headingRotation);
                    ImGuiMCP::InputFloat(std::format("Angle Factor##vis_{}", formID).c_str(), &editVis.angleFactor);

                    if (ImGuiMCP::Button(std::format("Save##vis_{:08X}", formID).c_str())) {
                        windFram->AddNewVisibilityConfig(editVis.formID, editVis.minVisibility, editVis.maxVisibility,
                                                         editVis.minWindStrength, editVis.maxWindStrength,
                                                         editVis.headingRotation, editVis.angleFactor);
                    }
                    ImGuiMCP::SameLine();
                    if (ImGuiMCP::Button(std::format("Remove##vis_{:08X}", formID).c_str())) {
                        windFram->RemoveVisibilityConfig(formID);
                        visEditBuf.erase(formID);
                    }
                }
            }

            // PushConfig
            if (config.pushConfig && showPushConf) {
                if (!pushEditBuf.contains(formID) || pushEditBuf[formID].formID != formID) {
                    pushEditBuf[formID] = *config.pushConfig;
                }
                auto& editPush = pushEditBuf[formID];
                if (editPush.formID != formID) editPush = *config.pushConfig;
                if (ImGuiMCP::CollapsingHeader(std::format("PushConfig##push_{}", formID).c_str())) {
                    ImGuiMCP::InputFloat(std::format("Wind Sensitivity##push_{}", formID).c_str(),
                                         &editPush.windSensitivity);

                    if (ImGuiMCP::Button(std::format("Save##push_{:08X}", formID).c_str())) {
                        windFram->AddNewPushConfig(editPush.formID, editPush.windSensitivity);
                    }
                    ImGuiMCP::SameLine();
                    if (ImGuiMCP::Button(std::format("Remove##push_{:08X}", formID).c_str())) {
                        windFram->RemovePushConfig(formID);
                        pushEditBuf.erase(formID);
                    }
                }
            }

            // TreeConfig
            if (config.treeConfig && showTreeConf) {
                if (!treeEditBuf.contains(formID)) {
                    treeEditBuf[formID] = config.treeConfig->max;
                }
                auto& editTreeMax = treeEditBuf[formID];
                if (ImGuiMCP::CollapsingHeader(std::format("TreeConfig (max only)##tree_{}", formID).c_str())) {
                    ImGuiMCP::InputFloat(std::format("Trunk Flexibility##tree_{}", formID).c_str(),
                                         &editTreeMax.trunkFlexibility);
                    ImGuiMCP::InputFloat(std::format("Branch Flexibility##tree_{}", formID).c_str(),
                                         &editTreeMax.branchFlexibility);
                    ImGuiMCP::InputFloat(std::format("Trunk Amplitude##tree_{}", formID).c_str(),
                                         &editTreeMax.trunkAmplitude);
                    ImGuiMCP::InputFloat(std::format("Front Amplitude##tree_{}", formID).c_str(),
                                         &editTreeMax.frontAmplitude);
                    ImGuiMCP::InputFloat(std::format("Back Amplitude##tree_{}", formID).c_str(),
                                         &editTreeMax.backAmplitude);
                    ImGuiMCP::InputFloat(std::format("Side Amplitude##tree_{}", formID).c_str(),
                                         &editTreeMax.sideAmplitude);
                    ImGuiMCP::InputFloat(std::format("Front Frequency##tree_{}", formID).c_str(),
                                         &editTreeMax.frontFrequency);
                    ImGuiMCP::InputFloat(std::format("Back Frequency##tree_{}", formID).c_str(),
                                         &editTreeMax.backFrequency);
                    ImGuiMCP::InputFloat(std::format("Side Frequency##tree_{}", formID).c_str(),
                                         &editTreeMax.sideFrequency);
                    ImGuiMCP::InputFloat(std::format("Leaf Flexibility##tree_{}", formID).c_str(),
                                         &editTreeMax.leafFlexibility);
                    ImGuiMCP::InputFloat(std::format("Leaf Amplitude##tree_{}", formID).c_str(),
                                         &editTreeMax.leafAmplitude);
                    ImGuiMCP::InputFloat(std::format("Leaf Frequency##tree_{}", formID).c_str(),
                                         &editTreeMax.leafFrequency);

                    if (ImGuiMCP::Button(std::format("Save##tree_{:08X}", formID).c_str())) {
                        TreeConfig newCfg = *config.treeConfig;
                        newCfg.max = editTreeMax;
                        auto* tree = RE::TESForm::LookupByID<RE::TESObjectTREE>(newCfg.formID);
                        if (tree) {
                            windFram->AddNewTreeConfig(tree, newCfg.max);
                        }
                    }
                    ImGuiMCP::SameLine();
                    if (ImGuiMCP::Button(std::format("Remove##tree_{:08X}", formID).c_str())) {
                        windFram->RemoveTreeConfig(formID);
                        treeEditBuf.erase(formID);
                    }
                }
            }

            // ModelSwapConfig
            if (config.modelSwapConfig && showModelConf) {
                if (!modelEditBuf.contains(formID)) {
                    modelEditBuf[formID] = *config.modelSwapConfig;
                }
                auto& editModelSwaps = modelEditBuf[formID].swaps;
                static float baseHeadingBuf = baseEditBuf[formID].headingRotation;
                static float baseAngleFactorBuf = baseEditBuf[formID].angleFactor;
                if (editModelSwaps.empty()) editModelSwaps = config.modelSwapConfig->swaps;
                if (ImGuiMCP::CollapsingHeader(std::format("ModelSwapConfig##model_{}", formID).c_str())) {
                    ImGuiMCP::InputFloat(std::format("Heading Rotation##base_{}", formID).c_str(), &baseHeadingBuf);
                    ImGuiMCP::InputFloat(std::format("Angle Factor##base_{}", formID).c_str(), &baseAngleFactorBuf);
                    for (size_t i = 0; i < editModelSwaps.size(); ++i) {
                        ImGuiMCP::InputFloat(std::format("Strength##model_{}_{}", formID, i).c_str(),
                                             &editModelSwaps[i].strength);
                        char buf[256];
                        strncpy_s(buf, editModelSwaps[i].modelPath.c_str(), sizeof(buf) - 1);
                        if (ImGuiMCP::InputText(std::format("ModelPath##model_{}_{}", formID, i).c_str(), buf,
                                                sizeof(buf))) {
                            editModelSwaps[i].modelPath = buf;
                        }
                        ImGuiMCP::SameLine();
                        if (ImGuiMCP::Button(std::format("Remove##model_{}_{}", formID, i).c_str())) {
                            editModelSwaps.erase(editModelSwaps.begin() + i);
                            break;
                        }
                    }
                    if (ImGuiMCP::Button(std::format("Save##model_{:08X}", formID).c_str())) {
                        windFram->AddNewModelSwapConfig(formID, baseHeadingBuf, baseAngleFactorBuf, editModelSwaps);
                    }
                    ImGuiMCP::SameLine();
                    if (ImGuiMCP::Button(std::format("Remove##model_{:08X}", formID).c_str())) {
                        windFram->RemoveModelSwapConfig(formID);
                        modelEditBuf.erase(formID);
                    }
                }
            }

            // BaseObjSwapConfig
            if (config.baseObjSwapConfig && showBaseConf) {
                if (!baseEditBuf.contains(formID)) {
                    baseEditBuf[formID] = *config.baseObjSwapConfig;
                }
                auto& editBaseSwaps = baseEditBuf[formID].swaps;
                static float baseHeadingBuf = baseEditBuf[formID].headingRotation;
                static float baseAngleFactorBuf = baseEditBuf[formID].angleFactor;
                if (ImGuiMCP::CollapsingHeader(std::format("BaseObjSwapConfig##base_{}", formID).c_str())) {
                    ImGuiMCP::InputFloat(std::format("Heading Rotation##base_{}", formID).c_str(),
                                         &baseHeadingBuf);
                    ImGuiMCP::InputFloat(std::format("Angle Factor##base_{}", formID).c_str(),
                                         &baseAngleFactorBuf);
                    for (size_t i = 0; i < editBaseSwaps.size(); ++i) {
                        ImGuiMCP::InputFloat(std::format("Strength##base_{}_{}", formID, i).c_str(),
                                             &editBaseSwaps[i].strength);
                        char buf[64];
                        strncpy_s(buf, Utils::FormIDToString(editBaseSwaps[i].formID).c_str(),
                                  sizeof(buf) - 1);
                        if (ImGuiMCP::InputText(std::format("FormID##base_{}_{}", formID, i).c_str(), buf,
                                                sizeof(buf))) {
                            if (auto baseformID = Utils::ParseForm(buf)) {
                                editBaseSwaps[i].formID = baseformID;
                            }
                        }
                        ImGuiMCP::SameLine();
                        if (ImGuiMCP::Button(std::format("Remove##base_{}_{}", formID, i).c_str())) {
                            editBaseSwaps.erase(editBaseSwaps.begin() + i);
                            break;
                        }
                    }
                    if (ImGuiMCP::Button(std::format("Save##base_{:08X}", formID).c_str())) {
                        windFram->AddNewBaseObjSwapConfig(formID, editBaseSwaps, baseHeadingBuf,
                                                          baseAngleFactorBuf);
                    }
                    ImGuiMCP::SameLine();
                    if (ImGuiMCP::Button(std::format("Remove##base_{:08X}", formID).c_str())) {
                        windFram->RemoveBaseObjSwapConfig(formID);
                        baseEditBuf.erase(formID);
                    }
                }
            }
        }
    }

    void __stdcall MCP::RenderLog() {
        auto* conf = Config::GetSingleton();
        ImGuiMCP::Checkbox("Trace", &MCPLog::log_trace);
        ImGuiMCP::SameLine();
        ImGuiMCP::Checkbox("Info", &MCPLog::log_info);
        ImGuiMCP::SameLine();
        ImGuiMCP::Checkbox("Warning", &MCPLog::log_warning);
        ImGuiMCP::SameLine();
        ImGuiMCP::Checkbox("Error", &MCPLog::log_error);
        ImGuiMCP::SameLine();
        ImGuiMCP::Checkbox("Time Logs", &conf->EnableTimeLogging);
        ImGuiMCP::InputText("Custom Filter", MCPLog::custom, 255);

        // if"Generate Log" button is pressed, read the log file
        if (ImGuiMCP::Button("Generate Log")) {
            logLines = MCPLog::ReadLogFile();
        }

        // Display each line in a new ImGuiMCP::Text() element
        for (const auto& line : logLines) {
            if (line.find("trace") != std::string::npos && !MCPLog::log_trace) continue;
            if (line.find("info") != std::string::npos && !MCPLog::log_info) continue;
            if (line.find("warning") != std::string::npos && !MCPLog::log_warning) continue;
            if (line.find("error") != std::string::npos && !MCPLog::log_error) continue;
            if (line.find(MCPLog::custom) == std::string::npos && MCPLog::custom != "") continue;
            ImGuiMCP::Text(line.c_str());
        }
    }
}

namespace MCPLog {
    std::filesystem::path GetLogPath() {
        const auto logsFolder = SKSE::log::log_directory();
        if (!logsFolder) SKSE::stl::report_and_fail("SKSE log_directory not provided, logs disabled.");
        auto pluginName = SKSE::PluginDeclaration::GetSingleton()->GetName();
        auto logFilePath = *logsFolder / std::format("{}.log", pluginName);
        return logFilePath;
    }

    std::vector<std::string> ReadLogFile() {
        std::vector<std::string> logLines;

        // Open the log file
        std::ifstream file(GetLogPath().c_str());
        if (!file.is_open()) {
            // Handle error
            return logLines;
        }

        // Read and store each line from the file
        std::string line;
        while (std::getline(file, line)) {
            logLines.push_back(line);
        }

        file.close();

        return logLines;
    }
}