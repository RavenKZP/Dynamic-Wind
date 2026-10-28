#ifndef DYNAMIC_WIND_PROVIDER_API_H
#define DYNAMIC_WIND_PROVIDER_API_H

#include <cstdint>
#include <type_traits>

// DLL boundary: fixed-layout data and function pointers; neither plugin frees the other's memory.
namespace DynamicWindAPI {
    inline constexpr uint32_t Version = 3;
    enum class Handler : uint32_t { Animation, Rotation, Visibility, Push, ModelSwap, BaseObjectSwap };
    enum class Event : uint32_t { Update, Initial, Target };
    enum class Result : uint32_t { Fallback, Ready, Defer };
    enum class PushResponse : uint32_t { Linear, Cubic };
    // Linear and Cubic push responses produce equal strength at this input magnitude.
    inline constexpr float PushReferenceStrength = 0.5f;
    enum Control : uint32_t { Transitions = 1, Gusts = 2, Trees = 4, GlobalWind = 8 };
    // Strength uses the host handler scale (1 is nominal full wind, not a cap); angle is atan2(y, x) in radians.
    struct Wind { float strength{}; float angle{}; };
    struct Vector { float x{}, y{}, z{}; };
    struct Frame { Wind baseline; float deltaTime{}; };
    struct Request { uint32_t formID{}; Handler handler{}; Event event{}; Vector position; };
    // Velocity uses the same strength scale in world XYZ; Push requests are sampled at body centers.
    struct Response {
        Wind wind;
        Result result{Result::Fallback};
        Vector velocity;
        // Push applies its curve to raw velocity, then this sensitivity multiplier.
        PushResponse pushResponse{PushResponse::Cubic};
        float pushSensitivity{1.0f};
    };

    // Callbacks are serialized by the host; do not reenter Dynamic Wind or retain request/response pointers.
    struct Provider {
        uint32_t size{sizeof(Provider)};
        uint32_t version{Version};
        void* context{};
        uint32_t controls{};
        bool (*IsEnabled)(void*) noexcept{};
        void (*BeginUpdate)(void*, float deltaTime) noexcept{};
        // Outputs start as Fallback; Defer on a Target request holds the entire target batch.
        void (*Sample)(void*, const Frame*, const Request*, Response*, uint32_t count) noexcept{};
        void (*EndUpdate)(void*) noexcept{};
        void (*TargetChanged)(void*) noexcept{};
        void (*Reset)(void*) noexcept{};
    };
    struct Interface {
        uint32_t size{sizeof(Interface)};
        uint32_t version{Version};
        // One provider per process; its context and callback code must outlive the game session.
        bool (*RegisterProvider)(const Provider*) noexcept{};
    };
    using RequestAPIFunction = const Interface* (*)(uint32_t version) noexcept;
    static_assert(sizeof(Wind) == 8 && sizeof(Request) == 24 && sizeof(Response) == 32);
    static_assert(std::is_standard_layout_v<Provider> && std::is_standard_layout_v<Interface>);
}

#endif
