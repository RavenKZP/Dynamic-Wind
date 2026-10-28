#pragma once

#include "DynamicWindAPI.h"
#include <span>

// Generic provider dispatch; handler behavior stays in Dynamic Wind when no provider supplies a response.
namespace WindProvider {
    bool IsEnabled();
    bool Controls(DynamicWindAPI::Control control);
    void BeginUpdate(float deltaTime);
    void Sample(const DynamicWindAPI::Frame& frame, std::span<const DynamicWindAPI::Request> requests,
                std::span<DynamicWindAPI::Response> responses);
    void EndUpdate();
    void TargetChanged();
    void Reset();
}
