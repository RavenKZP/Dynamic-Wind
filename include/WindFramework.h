#pragma once

#include "Settings.h"

#include "AnimationHandler.h"
#include "BaseObjSwapHandler.h"
#include "ModelSwapHandler.h"
#include "PushHandler.h"
#include "RotationHandler.h"
#include "TreeHandler.h"
#include "VisibilityHandler.h"
#include "WindProvider.h"

#include "REX/REX.h"

#include <cmath>
#include <unordered_set>
#include <shared_mutex>

struct WindObjectConfigs {
    const AnimationConfig* animationConfig = nullptr;
    const BaseObjSwapConfig* baseObjSwapConfig = nullptr;
    const ModelSwapConfig* modelSwapConfig = nullptr;
    const PushConfig* pushConfig = nullptr;
    const RotationConfig* rotationConfig = nullptr;
    const TreeConfig* treeConfig = nullptr;
    const VisibilityConfig* visibilityConfig = nullptr;
};

class WindFramework : public REX::Singleton<WindFramework> {
public:
    void Update(float strength, float angle, float deltaTime) {
        auto* conf = Config::GetSingleton();
        std::unique_lock lock(mutex_);
        WindProvider::BeginUpdate(deltaTime);
        const SKSE::stl::scope_exit endUpdate([] {
            WindProvider::EndUpdate();
        });
        _providerRequests.clear();
        _providerIndices.clear();
        const bool providerEnabled = WindProvider::IsEnabled();
        size_t targetStart = 0;
        if (providerEnabled) {
            const auto collect = [&](const auto& refs, DynamicWindAPI::Handler handler, DynamicWindAPI::Event event) {
                for (const auto& [formID, handle] : refs) {
                    const auto ref = handle.get();
                    if (!ref || ref->IsDisabled() || ref->IsDeleted()) continue;
                    const auto position = ref->GetPosition();
                    _providerIndices.emplace((uint64_t(handler) << 40) | (uint64_t(event) << 32) | formID, _providerRequests.size());
                    _providerRequests.push_back({formID, handler, event, {position.x, position.y, position.z}});
                    if (handler == DynamicWindAPI::Handler::Animation) AddAnimationSamples(ref.get(), event);
                }
            };
            if (conf->RotationHandlerEnabled)
                collect(_trackedRotationRefs, DynamicWindAPI::Handler::Rotation, DynamicWindAPI::Event::Update);
            if (conf->AnimationHandlerEnabled) collect(_trackedAnimationRefs, DynamicWindAPI::Handler::Animation, DynamicWindAPI::Event::Update);
            if (conf->VisibilityHandlerEnabled) collect(_trackedVisibilityRefs, DynamicWindAPI::Handler::Visibility, DynamicWindAPI::Event::Update);
            targetStart = _providerRequests.size();
            if (_pendingProviderTargets) {
                if (conf->RotationHandlerEnabled) collect(_trackedRotationRefs, DynamicWindAPI::Handler::Rotation, DynamicWindAPI::Event::Target);
                if (conf->ModelSwapHandlerEnabled) collect(_trackedModelSwapRefs, DynamicWindAPI::Handler::ModelSwap, DynamicWindAPI::Event::Target);
                if (conf->BaseObjSwapHandlerEnabled) collect(_trackedBaseObjSwapRefs, DynamicWindAPI::Handler::BaseObjectSwap, DynamicWindAPI::Event::Target);
            }
        }
        _providerResponses.resize(_providerRequests.size());
        const std::span requests{_providerRequests};
        const std::span responses{_providerResponses};
        if (targetStart)
            WindProvider::Sample({{strength, angle}, deltaTime}, requests.first(targetStart), responses.first(targetStart));
        if (targetStart < requests.size())
            WindProvider::Sample({_providerTargetWind, deltaTime}, requests.subspan(targetStart), responses.subspan(targetStart));
        bool targetsDeferred = false;
        for (size_t i = 0; i < _providerRequests.size(); ++i)
            targetsDeferred |= _providerRequests[i].event == DynamicWindAPI::Event::Target &&
                               _providerResponses[i].result == DynamicWindAPI::Result::Defer;
        if (conf->RotationHandlerEnabled) {
            for (auto it = _trackedRotationRefs.begin(); it != _trackedRotationRefs.end();) {
                auto ref = it->second.get().get();
                if (!ref) {
                    it = _trackedRotationRefs.erase(it);
                } else {
                    if (ref->IsDisabled() || ref->IsDeleted()) {
                        it = _trackedRotationRefs.erase(it);
                    } else {
                        const auto local = GetProviderWind(ref, DynamicWindAPI::Handler::Rotation, {strength, angle});
                        rotationHandler_.Apply(ref, local.wind.strength, local.wind.angle);
                        if (_pendingProviderTargets && !targetsDeferred) {
                            const auto target = GetProviderWind(ref, DynamicWindAPI::Handler::Rotation, _providerTargetWind, DynamicWindAPI::Event::Target);
                            rotationHandler_.Apply(ref, target.wind.strength, target.wind.angle, true);
                        }
                        ++it;
                    }
                }
            }
        }
        if (conf->AnimationHandlerEnabled) {
            for (auto it = _trackedAnimationRefs.begin(); it != _trackedAnimationRefs.end();) {
                auto ref = it->second.get().get();
                if (!ref) {
                    it = _trackedAnimationRefs.erase(it);
                } else {
                    if (ref->IsDisabled() || ref->IsDeleted()) {
                        it = _trackedAnimationRefs.erase(it);
                    } else {
                        const auto local = GetProviderWind(ref, DynamicWindAPI::Handler::Animation, {strength, angle});
                        const bool watched = _animationDiagnostics.formID == ref->GetFormID();
                        animationHandler_.Apply(ref, local.wind.strength, local.wind.angle,
                                                watched ? &_animationDiagnostics.lastUpdate : nullptr);
                        if (watched) {
                            ++_animationDiagnostics.updates;
                            if (local.result == DynamicWindAPI::Result::Ready) ++_animationDiagnostics.providerUpdates;
                            _animationDiagnostics.lastWind = local;
                            _animationDiagnostics.peakStrength = std::max(_animationDiagnostics.peakStrength, local.wind.strength);
                            _animationDiagnostics.controllerWrites += _animationDiagnostics.lastUpdate.controllerWrites;
                        }
                        ++it;
                    }
                }
            }
        }
        if (conf->VisibilityHandlerEnabled) {
            for (auto it = _trackedVisibilityRefs.begin(); it != _trackedVisibilityRefs.end();) {
                auto ref = it->second.get().get();
                if (!ref) {
                    it = _trackedVisibilityRefs.erase(it);
                } else {
                    if (ref->IsDisabled() || ref->IsDeleted()) {
                        it = _trackedVisibilityRefs.erase(it);
                    } else {
                        const auto local = GetProviderWind(ref, DynamicWindAPI::Handler::Visibility, {strength, angle});
                        visibilityHandler_.Apply(ref, local.wind.strength, local.wind.angle);
                        ++it;
                    }
                }
            }
        }
        if (conf->PushHandlerEnabled) {
            for (auto it = _trackedPushRefs.begin(); it != _trackedPushRefs.end();) {
                auto ref = it->second.get().get();
                if (!ref) {
                    it = _trackedPushRefs.erase(it);
                } else {
                    if (ref->IsDisabled() || ref->IsDeleted()) {
                        it = _trackedPushRefs.erase(it);
                    } else {
                        pushHandler_.Apply(ref, strength, angle, deltaTime);
                        ++it;
                    }
                }
            }
        }
        if (_pendingProviderTargets && !targetsDeferred) {
            const auto applyTargets = [&](auto& refs, auto& handler, DynamicWindAPI::Handler kind) {
                for (auto it = refs.begin(); it != refs.end();) {
                    const auto ref = it->second.get();
                    if (!ref || ref->IsDisabled() || ref->IsDeleted()) it = refs.erase(it);
                    else {
                        const auto local = GetProviderWind(ref.get(), kind, _providerTargetWind, DynamicWindAPI::Event::Target);
                        handler.Apply(ref.get(), local.wind.strength, local.wind.angle);
                        ++it;
                    }
                }
            };
            if (conf->ModelSwapHandlerEnabled) applyTargets(_trackedModelSwapRefs, modelSwapHandler_, DynamicWindAPI::Handler::ModelSwap);
            if (conf->BaseObjSwapHandlerEnabled) applyTargets(_trackedBaseObjSwapRefs, baseObjSwapHandler_, DynamicWindAPI::Handler::BaseObjectSwap);
            _pendingProviderTargets = false;
        }
    }

    void NewTargets(float strength, float angle) {
        auto* conf = Config::GetSingleton();

        std::unique_lock lock(mutex_);

        // A provider controlling trees must not receive competing tree parameter writes.
        if (conf->TreeHandlerEnabled && !WindProvider::Controls(DynamicWindAPI::Trees)) {
            treeHandler_.Update(strength, angle);
        }
        if (WindProvider::IsEnabled()) {
            WindProvider::TargetChanged();
            _providerTargetWind = {strength, angle};
            _pendingProviderTargets = true;
            return;
        }
        _pendingProviderTargets = false;
        if (conf->RotationHandlerEnabled) {
            for (auto it = _trackedRotationRefs.begin(); it != _trackedRotationRefs.end();) {
                auto ref = it->second.get().get();
                if (!ref) {
                    it = _trackedRotationRefs.erase(it);
                } else {
                    if (ref->IsDisabled() || ref->IsDeleted()) {
                        it = _trackedRotationRefs.erase(it);
                    } else {
                        rotationHandler_.Apply(ref, strength, angle, true);
                        ++it;
                    }
                }
            }
        }

        if (conf->ModelSwapHandlerEnabled) {
            for (auto it = _trackedModelSwapRefs.begin(); it != _trackedModelSwapRefs.end();) {
                auto ref = it->second.get().get();
                if (!ref) {
                    it = _trackedModelSwapRefs.erase(it);
                } else {
                    if (ref->IsDisabled() || ref->IsDeleted()) {
                        it = _trackedModelSwapRefs.erase(it);
                    } else {
                        modelSwapHandler_.Apply(ref, strength, angle);
                        ++it;
                    }
                }
            }
        }

        if (conf->BaseObjSwapHandlerEnabled) {
            for (auto it = _trackedBaseObjSwapRefs.begin(); it != _trackedBaseObjSwapRefs.end();) {
                auto ref = it->second.get().get();
                if (!ref) {
                    it = _trackedBaseObjSwapRefs.erase(it);
                } else {
                    if (ref->IsDisabled() || ref->IsDeleted()) {
                        it = _trackedBaseObjSwapRefs.erase(it);
                    } else {
                        baseObjSwapHandler_.Apply(ref, strength, angle);
                        ++it;
                    }
                }
            }
        }
    }

    /// Releases transient handler state without changing global tree settings.
    void Suspend() {
        std::unique_lock lock(mutex_);
        WindProvider::Reset();
        _pendingProviderTargets = false;
    }

    void DisableMod() {
        Suspend();
        std::unique_lock lock(mutex_);
        // Resetting the mod must not write tree values owned by an external provider.
        if (!WindProvider::Controls(DynamicWindAPI::Trees)) treeHandler_.Update(0, 0);
    }

    void RefLoad(RE::TESObjectREFR* ref, float angle = 0.0f, float strength = 0.0f) {
        if (!ref) return;

        if (!Utils::IsInWindCell(ref)) return;

        auto baseObj = ref->GetBaseObject();
        if (!baseObj) return;

        auto baseFormID = baseObj->GetFormID();
        auto formID = ref->GetFormID();

        auto* conf = Config::GetSingleton();

        std::unique_lock lock(mutex_);
        if (conf->AnimationHandlerEnabled && animationHandler_.HasConfig(ref)) {
            _trackedAnimationRefs.try_emplace(formID, ref->GetHandle());
        }
        if (conf->RotationHandlerEnabled && (rotationHandler_.HasConfig(baseFormID) || rotationHandler_.HasConfig(formID))) {
            if (_trackedRotationRefs.try_emplace(formID, ref->GetHandle()).second) {
                const auto local = GetInitialProviderWind(ref, DynamicWindAPI::Handler::Rotation, {strength, angle});
                rotationHandler_.Apply(ref, local.wind.strength, local.wind.angle, true);
            }
        }
        if (conf->VisibilityHandlerEnabled && (visibilityHandler_.HasConfig(baseFormID) || visibilityHandler_.HasConfig(formID))) {
            _trackedVisibilityRefs.try_emplace(formID, ref->GetHandle());
        }
        if (conf->PushHandlerEnabled && (pushHandler_.HasConfig(baseFormID) || pushHandler_.HasConfig(formID))) {
            _trackedPushRefs.try_emplace(formID, ref->GetHandle());
        }
        if (conf->ModelSwapHandlerEnabled && !_trackedBaseObjSwapRefs.contains(formID) &&
            (modelSwapHandler_.HasConfig(baseFormID) || modelSwapHandler_.HasConfig(formID))) {
            if (_trackedModelSwapRefs.try_emplace(formID, ref->GetHandle()).second) {
                const auto local = GetInitialProviderWind(ref, DynamicWindAPI::Handler::ModelSwap, {strength, angle});
                modelSwapHandler_.Apply(ref, local.wind.strength, local.wind.angle);
            }
        } else if (conf->BaseObjSwapHandlerEnabled && !_trackedModelSwapRefs.contains(formID) &&
                   (baseObjSwapHandler_.HasConfig(baseFormID) || baseObjSwapHandler_.HasConfig(formID))) {
            if (_trackedBaseObjSwapRefs.try_emplace(formID, ref->GetHandle()).second) {
                const auto local = GetInitialProviderWind(ref, DynamicWindAPI::Handler::BaseObjectSwap, {strength, angle});
                baseObjSwapHandler_.Apply(ref, local.wind.strength, local.wind.angle);
            }
        }

        // --- other object types TBD ---
    }

    /// Watches one reference and reports its matching, tracking, sampling and actual animation writes.
    std::string GetAnimationDiagnostics(RE::TESObjectREFR* ref) {
        std::unique_lock lock(mutex_);
        if (!ref || !ref->GetBaseObject()) return "No reference with a base object selected.";
        const auto formID = ref->GetFormID();
        const auto baseID = ref->GetBaseObject()->GetFormID();
        if (_animationDiagnostics.formID != formID) _animationDiagnostics = {formID};
        const auto* config = animationHandler_.GetMatchingConfig(ref);
        const auto* settings = Config::GetSingleton();
        std::string text = std::format("Reference: {:08X} | Base: {:08X}\nNIF: {}\nLoaded animation rules: {} | Indexed NIF paths: {}\n",
                                      formID, baseID, Utils::GetModelPath(ref->GetBaseObject()),
                                      animationHandler_.GetConfigs().size(), animationHandler_.GetModelConfigCount());
        if (config) {
            const char* match = config->formID == formID ? "reference ID" : config->formID == baseID ? "base ID" : "NIF match";
            text += std::format("Matched rule: {:08X} via {}\nRule file: {}\nConfigured speed: {:.3f} to {:.3f}\n",
                                config->formID, match, config->filePath, config->speedMin, config->speedMax);
        } else {
            text += "Matched rule: NONE\n";
        }
        text += std::format("DWF enabled: {} | Animation handler enabled: {}\n"
                            "3D loaded: {} | Reference disabled: {} | Deleted: {} | Wind cell: {}\n"
                            "Tracked by DWF animation handler: {}\n",
                            settings->ModActive ? "Yes" : "No", settings->AnimationHandlerEnabled ? "Yes" : "No",
                            ref->Get3D() ? "Yes" : "No", ref->IsDisabled() ? "Yes" : "No", ref->IsDeleted() ? "Yes" : "No",
                            Utils::IsInWindCell(ref) ? "Yes" : "No", _trackedAnimationRefs.contains(formID) ? "YES" : "NO");
        const auto& diagnostics = _animationDiagnostics;
        text += std::format("Updates since inspection began: {}\nExternal wind updates: {} | Native/fallback updates: {}\n",
                            diagnostics.updates, diagnostics.providerUpdates, diagnostics.updates - diagnostics.providerUpdates);
        if (diagnostics.updates) {
            text += std::format("Last input: {} | Wind strength: {:.4f} | Peak strength: {:.4f}\n"
                                "Last computed animation speed: {}\n"
                                "Controller writes last update: {} | Total writes: {}\n",
                                diagnostics.lastWind.result == DynamicWindAPI::Result::Ready ? "external provider" : "native/fallback",
                                diagnostics.lastWind.wind.strength, diagnostics.peakStrength,
                                diagnostics.lastUpdate.evaluated ? std::format("{:.4f}", diagnostics.lastUpdate.speed) : "not calculated",
                                diagnostics.lastUpdate.controllerWrites, diagnostics.controllerWrites);
        }
        text += "Counters continue while the menu is closed; selecting another reference resets them.";
        return text;
    }

    std::map<RE::FormID, WindObjectConfigs> GetConfigs() {
        std::map<RE::FormID, WindObjectConfigs> result;

        
        std::shared_lock lock(mutex_);
        // Animation
        for (const auto& [formID, cfg] : animationHandler_.GetConfigs()) {
            result[formID].animationConfig = &cfg;
        }
        // BaseObjSwap
        for (const auto& [formID, cfg] : baseObjSwapHandler_.GetConfigs()) {
            result[formID].baseObjSwapConfig = &cfg;
        }
        // ModelSwap
        for (const auto& [formID, cfg] : modelSwapHandler_.GetConfigs()) {
            result[formID].modelSwapConfig = &cfg;
        }
        // Push
        for (const auto& [formID, cfg] : pushHandler_.GetConfigs()) {
            result[formID].pushConfig = &cfg;
        }
        // Rotation
        for (const auto& [formID, cfg] : rotationHandler_.GetConfigs()) {
            result[formID].rotationConfig = &cfg;
        }
        // Tree
        for (const auto& [formID, cfg] : treeHandler_.GetConfigs()) {
            result[formID].treeConfig = &cfg;
        }
        // Visibility
        for (const auto& [formID, cfg] : visibilityHandler_.GetConfigs()) {
            result[formID].visibilityConfig = &cfg;
        }

        return result;
    }

    void AddNewTreeConfig(const RE::TESObjectTREE* tree, const TreeDataConfig& cfg) {
        treeHandler_.AddNewConfig(tree, cfg);
    }
    TreeConfig GetTreeConfig(const RE::FormID formID) { return treeHandler_.GetConfig(formID); }
    bool HasTreeConfig(const RE::FormID formID) { return treeHandler_.HasConfig(formID); }
    void RemoveTreeConfig(const RE::FormID formID) { treeHandler_.RemoveConfig(formID); }

    void AddNewAnimationConfig(const RE::FormID formID, float minAnimSpeed, float maxAnimSpeed, float headingRotation,
                               float angleFactor) {
        animationHandler_.AddNewConfig(formID, minAnimSpeed, maxAnimSpeed, headingRotation, angleFactor);
    }
    void AddNewAnimationConfig(const RE::TESBoundObject* obj, float minAnimSpeed, float maxAnimSpeed,
                               float headingRotation, float angleFactor) {
        animationHandler_.AddNewConfig(obj->GetFormID(), minAnimSpeed, maxAnimSpeed, headingRotation, angleFactor);
    }
    void AddNewAnimationConfig(const RE::TESObjectREFR* ref, float minAnimSpeed, float maxAnimSpeed,
                               float headingRotation, float angleFactor) {
        if (!ref->IsDynamicForm()) {
            animationHandler_.AddNewConfig(ref->GetFormID(), minAnimSpeed, maxAnimSpeed, headingRotation, angleFactor);
        }
    }
    void RemoveAnimationConfig(const RE::FormID formID) { animationHandler_.RemoveConfig(formID); }

    void AddNewRotationConfig(const RE::FormID formID, float headingRotation,
                              std::optional<std::vector<float>> allowedAngles = std::nullopt) {
        rotationHandler_.AddNewConfig(formID, headingRotation, allowedAngles);
    }
    void AddNewRotationConfig(const RE::TESBoundObject* obj, float headingRotation,
                              std::optional<std::vector<float>> allowedAngles = std::nullopt) {
        rotationHandler_.AddNewConfig(obj->GetFormID(), headingRotation, allowedAngles);
    }
    void AddNewRotationConfig(const RE::TESObjectREFR* ref, float headingRotation,
                              std::optional<std::vector<float>> allowedAngles = std::nullopt) {
        if (!ref->IsDynamicForm()) {
            rotationHandler_.AddNewConfig(ref->GetFormID(), headingRotation, allowedAngles);
        }
    }
    void RemoveRotationConfig(const RE::FormID formID) { rotationHandler_.RemoveConfig(formID); }

    void AddNewVisibilityConfig(const RE::FormID formID, float minVisibility, float maxVisibility,
                                float minWindStrength, float maxWindStrength, float headingRotation,
                                float angleFactor) {
        visibilityHandler_.AddNewConfig(formID, minVisibility, maxVisibility, minWindStrength, maxWindStrength,
                                        headingRotation, angleFactor);
    }
    void AddNewVisibilityConfig(const RE::TESBoundObject* obj, float minVisibility, float maxVisibility,
                                float minWindStrength, float maxWindStrength, float headingRotation,
                                float angleFactor) {
        visibilityHandler_.AddNewConfig(obj->GetFormID(), minVisibility, maxVisibility, minWindStrength,
                                        maxWindStrength,
                                        headingRotation, angleFactor);
    }
    void AddNewVisibilityConfig(const RE::TESObjectREFR* ref, float minVisibility, float maxVisibility,
                                float minWindStrength, float maxWindStrength, float headingRotation, float angleFactor) {
        if (!ref->IsDynamicForm()) {
            visibilityHandler_.AddNewConfig(ref->GetFormID(), minVisibility, maxVisibility, minWindStrength,
                                            maxWindStrength,
                                            headingRotation, angleFactor);
        }
    }
    void RemoveVisibilityConfig(const RE::FormID formID) { visibilityHandler_.RemoveConfig(formID); }
    
    void AddNewPushConfig(const RE::FormID formID, float windSensitivity) {
        pushHandler_.AddNewConfig(formID, windSensitivity);
    }
    void AddNewPushConfig(const RE::TESBoundObject* obj, float windSensitivity) {
        pushHandler_.AddNewConfig(obj->GetFormID(), windSensitivity);
    }
    void AddNewPushConfig(const RE::TESObjectREFR* ref, float windSensitivity) {
        if (!ref->IsDynamicForm()) {
            pushHandler_.AddNewConfig(ref->GetFormID(), windSensitivity);
        }
    }
    void RemovePushConfig(const RE::FormID formID) { pushHandler_.RemoveConfig(formID); }

    void AddNewModelSwapConfig(const RE::FormID formID, float headingRotation, float angleFactor,
                               std::vector<ModelSwapEntry>& swaps) {
        modelSwapHandler_.AddNewConfig(formID, headingRotation, angleFactor, swaps);
    }
    void AddNewModelSwapConfig(const RE::TESBoundObject* obj, float headingRotation, float angleFactor,
                               std::vector<ModelSwapEntry>& swaps) {
        modelSwapHandler_.AddNewConfig(obj->GetFormID(), headingRotation, angleFactor, swaps);
    }
    void AddNewModelSwapConfig(const RE::TESObjectREFR* ref, float headingRotation, float angleFactor,
                               std::vector<ModelSwapEntry>& swaps) {
        if (!ref->IsDynamicForm()) {
            modelSwapHandler_.AddNewConfig(ref->GetFormID(), headingRotation, angleFactor, swaps);
        }
    }
    void RemoveModelSwapConfig(const RE::FormID formID) { modelSwapHandler_.RemoveConfig(formID); }

    void AddNewBaseObjSwapConfig(const RE::FormID formID, const std::vector<BaseObjSwapEntry>& swaps,
                                 float headingRotation, float angleFactor) {
        baseObjSwapHandler_.AddNewConfig(formID, swaps, headingRotation, angleFactor);
    }
    void RemoveBaseObjSwapConfig(const RE::FormID formID) { baseObjSwapHandler_.RemoveConfig(formID); }

private:
    struct AnimationDiagnostics {
        RE::FormID formID{};
        uint64_t updates{};
        uint64_t providerUpdates{};
        uint64_t controllerWrites{};
        float peakStrength{};
        DynamicWindAPI::Response lastWind;
        AnimationUpdateDiagnostics lastUpdate;
    } _animationDiagnostics;

    void AddAnimationSamples(RE::TESObjectREFR* ref, DynamicWindAPI::Event event) {
        const auto* root = ref->Get3D();
        if (!root || !ref->GetBaseObject()) return;
        const auto min = ref->GetBoundMin();
        const auto max = ref->GetBoundMax();
        const auto finite = [](const RE::NiPoint3& point) {
            return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
        };
        if (!finite(min) || !finite(max) || min.x > max.x || min.y > max.y || min.z > max.z || min == max) return;
        const auto center = min + (max - min) * 0.5f;
        for (const float x : {min.x, center.x, max.x})
            for (const float y : {min.y, center.y, max.y})
                for (const float z : {min.z, center.z, max.z}) {
                    const auto position = root->world * RE::NiPoint3{x, y, z};
                    if (finite(position))
                        _providerRequests.push_back({ref->GetFormID(), DynamicWindAPI::Handler::Animation, event,
                                                     {position.x, position.y, position.z}});
                }
    }

    // Missing or deferred continuous samples preserve the original handler inputs.
    DynamicWindAPI::Response GetProviderWind(const RE::TESObjectREFR* ref, DynamicWindAPI::Handler handler, DynamicWindAPI::Wind fallback,
                                            DynamicWindAPI::Event event = DynamicWindAPI::Event::Update) const {
        const auto index = _providerIndices.find((uint64_t(handler) << 40) | (uint64_t(event) << 32) | ref->GetFormID());
        if (index != _providerIndices.end()) {
            auto response = _providerResponses[index->second];
            if (handler == DynamicWindAPI::Handler::Animation) {
                for (size_t i = index->second + 1; i < _providerResponses.size(); ++i) {
                    const auto& request = _providerRequests[i];
                    if (request.handler != handler || request.formID != ref->GetFormID()) break;
                    const auto& sample = _providerResponses[i];
                    if (sample.result == DynamicWindAPI::Result::Ready &&
                        (response.result != DynamicWindAPI::Result::Ready || sample.wind.strength > response.wind.strength))
                        response = sample;
                }
            }
            if (response.result == DynamicWindAPI::Result::Ready) return response;
        }
        return {fallback, DynamicWindAPI::Result::Fallback};
    }

    // Initial variants may use provider inputs before their first frame update.
    DynamicWindAPI::Response GetInitialProviderWind(const RE::TESObjectREFR* ref, DynamicWindAPI::Handler handler,
                                                   DynamicWindAPI::Wind fallback) {
        const auto position = ref->GetPosition();
        const DynamicWindAPI::Request request{ref->GetFormID(), handler, DynamicWindAPI::Event::Initial,
                                              {position.x, position.y, position.z}};
        DynamicWindAPI::Response response;
        WindProvider::Sample({fallback, 0.0f}, {&request, 1}, {&response, 1});
        return response.result == DynamicWindAPI::Result::Ready ? response :
            DynamicWindAPI::Response{fallback, DynamicWindAPI::Result::Fallback};
    }

    AnimationHandler animationHandler_;
    BaseObjSwapHandler baseObjSwapHandler_;
    ModelSwapHandler modelSwapHandler_;
    PushHandler pushHandler_;
    RotationHandler rotationHandler_;
    TreeHandler treeHandler_;
    VisibilityHandler visibilityHandler_;

    bool _pendingProviderTargets = false;
    DynamicWindAPI::Wind _providerTargetWind;
    std::vector<DynamicWindAPI::Request> _providerRequests;
    std::vector<DynamicWindAPI::Response> _providerResponses;
    std::unordered_map<uint64_t, std::size_t> _providerIndices;
    std::shared_mutex mutex_;
    std::unordered_map<RE::FormID, RE::ObjectRefHandle> _trackedAnimationRefs;
    std::unordered_map<RE::FormID, RE::ObjectRefHandle> _trackedBaseObjSwapRefs;
    std::unordered_map<RE::FormID, RE::ObjectRefHandle> _trackedModelSwapRefs;
    std::unordered_map<RE::FormID, RE::ObjectRefHandle> _trackedPushRefs;
    std::unordered_map<RE::FormID, RE::ObjectRefHandle> _trackedRotationRefs;
    std::unordered_map<RE::FormID, RE::ObjectRefHandle> _trackedVisibilityRefs;
};
