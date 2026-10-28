#include "Hooks.h"
#include "Settings.h"
#include "WindManager.h"
#include "WindFramework.h"

namespace Hooks {
    void UpdateHook::Update(RE::Actor* a_this, float a_delta) {
        Update_(a_this, a_delta);
        auto* conf = Config::GetSingleton();
        if (conf->ModActive) {
            auto start = std::chrono::high_resolution_clock::now();

            Wind::Manager::GetSingleton()->Update(a_this, a_delta);

            if (conf->EnableTimeLogging) {
                auto end = std::chrono::high_resolution_clock::now();
                std::chrono::duration<double, std::milli> elapsed = end - start;
                logger::debug("Wind::Manager Update {} ms", elapsed.count());
            }
        }
    }

    RE::NiAVObject* RefLoadHook::Load3D(RE::TESObjectREFR* a_this, bool a_backgroundLoading) {
        auto res = Load3D_(a_this, a_backgroundLoading);
        auto* conf = Config::GetSingleton();
        if (conf->ModActive) {
            SKSE::GetTaskInterface()->AddTask([handle = a_this->GetHandle()]() {
                const auto ref = handle.get();
                if (!ref || !Config::GetSingleton()->ModActive) return;
                auto start = std::chrono::high_resolution_clock::now();
                auto [angle, strength] = Wind::Manager::GetSingleton()->GetTargets();
                WindFramework::GetSingleton()->RefLoad(ref.get(), angle, strength);
                if (Config::GetSingleton()->EnableTimeLogging) {
                    auto end = std::chrono::high_resolution_clock::now();
                    std::chrono::duration<double, std::milli> elapsed = end - start;
                    logger::debug("WindFramework RefLoad {} ms", elapsed.count());
                }
            });
        }
        return res;
    }
}
