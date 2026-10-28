#pragma once

#define M_PI 3.14159265358979323846f

namespace DynamicWindAPI { struct Frame; }

namespace Utils {
    std::string FormIDToString(RE::FormID formID);
    RE::FormID ParseForm(const std::string& str);


    /// Applies animation speed and optionally counts controller frequency writes.
    void ApplySpeedToNode(RE::NiAVObject* node, float speed, uint32_t* controllerWrites = nullptr);

    void ApplyImpulseToNode(RE::NiAVObject* node, RE::hkVector4 velocity, const DynamicWindAPI::Frame& frame,
                            RE::FormID formID, float impulseScale);

    void ReplaceModel(RE::TESObjectREFR* ref, std::string modelPath);

    void ReplaceBaseObject(RE::TESObjectREFR* ref, RE::TESBoundObject* newBase);

    std::string GetModelPath(RE::TESBoundObject* base);

    bool IsInWindCell(RE::TESObjectREFR* ref);
}
