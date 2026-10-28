#include "WindProvider.h"

#include <cmath>
#include <mutex>
#include <numbers>

namespace {
    std::mutex providerMutex;
    DynamicWindAPI::Provider provider{};
    bool registered = false;
    bool wasEnabled = false;
    constexpr uint32_t SupportedControls = DynamicWindAPI::Transitions | DynamicWindAPI::Gusts |
                                           DynamicWindAPI::Trees | DynamicWindAPI::GlobalWind;

    bool Enabled() { return registered && provider.IsEnabled(provider.context); }

    bool RegisterProvider(const DynamicWindAPI::Provider* value) noexcept {
        std::scoped_lock lock(providerMutex);
        if (registered || !value || value->size != sizeof(*value) || value->version != DynamicWindAPI::Version ||
            !value->IsEnabled || !value->Sample || (value->controls & ~SupportedControls) != 0) return false;
        provider = *value;
        registered = true;
        return true;
    }
    const DynamicWindAPI::Interface api{sizeof(DynamicWindAPI::Interface), DynamicWindAPI::Version, RegisterProvider};
}

// Providers obtain this versioned interface after SKSE has loaded all plugin DLLs.
extern "C" __declspec(dllexport) const DynamicWindAPI::Interface* RequestDynamicWindAPI(uint32_t version) noexcept {
    return version == DynamicWindAPI::Version ? &api : nullptr;
}

namespace WindProvider {
    bool IsEnabled() {
        std::scoped_lock lock(providerMutex);
        return Enabled();
    }
    bool Controls(DynamicWindAPI::Control control) {
        std::scoped_lock lock(providerMutex);
        return Enabled() && (provider.controls & uint32_t(control)) != 0;
    }
    void BeginUpdate(float deltaTime) {
        std::scoped_lock lock(providerMutex);
        const bool enabled = Enabled();
        if (!enabled && wasEnabled && provider.Reset) provider.Reset(provider.context);
        wasEnabled = enabled;
        if (enabled && provider.BeginUpdate) provider.BeginUpdate(provider.context, deltaTime);
    }
    void Sample(const DynamicWindAPI::Frame& frame, std::span<const DynamicWindAPI::Request> requests,
                std::span<DynamicWindAPI::Response> responses) {
        std::scoped_lock lock(providerMutex);
        for (auto& response : responses) response = {frame.baseline, DynamicWindAPI::Result::Fallback};
        if (!Enabled() || requests.size() != responses.size() || requests.size() > UINT32_MAX) return;
        provider.Sample(provider.context, &frame, requests.data(), responses.data(), uint32_t(requests.size()));
        // Bad provider output must leave the original handlers usable rather than passing NaNs to the engine.
        for (auto& response : responses) {
            if (response.result != DynamicWindAPI::Result::Ready && response.result != DynamicWindAPI::Result::Defer)
                response = {frame.baseline, DynamicWindAPI::Result::Fallback};
            else if (!std::isfinite(response.wind.strength) || response.wind.strength < 0.0f ||
                     !std::isfinite(response.wind.angle) || !std::isfinite(response.velocity.x) ||
                     !std::isfinite(response.velocity.y) || !std::isfinite(response.velocity.z) ||
                     (response.pushResponse != DynamicWindAPI::PushResponse::Linear && response.pushResponse != DynamicWindAPI::PushResponse::Cubic) ||
                     !std::isfinite(response.pushSensitivity) || response.pushSensitivity < 0.0f)
                response = {frame.baseline, DynamicWindAPI::Result::Fallback};
            else response.wind.angle = std::remainder(response.wind.angle, 2.0f * std::numbers::pi_v<float>);
        }
    }
    void EndUpdate() {
        std::scoped_lock lock(providerMutex);
        if (wasEnabled && provider.EndUpdate) provider.EndUpdate(provider.context);
    }
    void TargetChanged() {
        std::scoped_lock lock(providerMutex);
        if (Enabled() && provider.TargetChanged) provider.TargetChanged(provider.context);
    }
    void Reset() {
        std::scoped_lock lock(providerMutex);
        if (registered && provider.Reset) provider.Reset(provider.context);
        wasEnabled = false;
    }
}
