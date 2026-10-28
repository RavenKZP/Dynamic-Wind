#include "Utils.h"

#include "Hooks.h"
#include "WindProvider.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Utils {
    std::string FormIDToString(RE::FormID formID) {
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            return "0x0~Unknown";
        }

        uint8_t highByte = (formID >> 24) & 0xFF;

        if (highByte == 0xFE) {  // ESL
            uint16_t eslIndex = (formID >> 12) & 0xFFF;
            uint16_t localID = formID & 0xFFF;

            for (auto* file : dataHandler->files) {
                if (file && file->IsLight() && file->smallFileCompileIndex == eslIndex) {
                    return std::format("0x{:03X}~{}", localID, file->fileName);
                }
            }
        } else {  // regular plugin
            uint32_t localID = formID & 0xFFFFFF;

            for (auto* file : dataHandler->files) {
                if (file && file->compileIndex == highByte) {
                    return std::format("0x{:06X}~{}", localID, file->fileName);
                }
            }
        }
        return std::format("0x{:08X}~Unknown", formID);
    }

    RE::FormID ParseForm(const std::string& str) {
        auto pos = str.find('~');
        if (pos == std::string::npos) {
            return 0;
        }

        std::string idPart = str.substr(0, pos);
        std::string modName = str.substr(pos + 1);

        RE::FormID localID = std::stoul(idPart, nullptr, 16);

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            return 0;
        }

        const RE::TESFile* mod = dataHandler->LookupModByName(modName);
        if (!mod) {
            logger::error("Can't find mod: {}", modName);
            return 0;
        }

        // ESL (light plugin)
        if (mod->IsLight()) {
            localID &= 0xFFF;  // only lower 12 bits
        }

        RE::TESForm* form = dataHandler->LookupForm(localID, modName);
        // fallback to manual ID construction if form lookup fails
        // this is a case for unloaded references
        if (!form) {
            auto modIndex =
                mod->IsLight() ? (0xFE << 12) | (mod->smallFileCompileIndex & 0xFFF) : (mod->compileIndex & 0xFF);
            auto fullID = (modIndex << 24) | localID;
            return fullID;
        }

        return form->GetFormID();
    }

    void ApplySpeedToNode(RE::NiAVObject* node, float speed, uint32_t* controllerWrites) {
        if (!node) return;

        // Check controller on this node
        if (auto* contMgr = node->GetController<RE::NiControllerManager>()) {
            for (auto* seq : contMgr->activeSequences) {
                if (seq) {
                    seq->frequency = speed;
                    if (controllerWrites) ++*controllerWrites;
                }
            }
        } else if (auto* contTime = node->GetController<RE::NiTimeController>()) {
            contTime->frequency = speed;
            if (controllerWrites) ++*controllerWrites;
        }

        // If it's a NiNode recurse into children
        if (auto* niNode = node->AsNode()) {
            for (auto& child : niNode->GetChildren()) {
                ApplySpeedToNode(child.get(), speed, controllerWrites);
            }
        }
    }

    void ApplyImpulseToNode(RE::NiAVObject* node, RE::hkVector4 velocity, const DynamicWindAPI::Frame& frame,
                            RE::FormID formID, float impulseScale) {
        if (!node) return;
        if (auto* collisionObject = node->GetCollisionObject()) {
            if (auto* rigidBody = collisionObject->body->AsBhkRigidBody()) {
                auto localVelocity = velocity;
                if (WindProvider::IsEnabled()) {
                    RE::hkVector4 centerOfMass;
                    if (auto* world = rigidBody->GetWorld2(); world && world->userData) {
                        RE::BSReadLockGuard lock(world->userData->worldLock);
                        rigidBody->GetCenterOfMassWorld(centerOfMass);
                    } else {
                        rigidBody->GetCenterOfMassWorld(centerOfMass);
                    }
                    const auto& center = centerOfMass.quad.m128_f32;
                    const float worldScale = RE::bhkWorld::GetWorldScaleInverse();
                    const DynamicWindAPI::Vector position{center[0] * worldScale, center[1] * worldScale, center[2] * worldScale};
                    if (std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z)) {
                        const std::array requests{DynamicWindAPI::Request{
                            formID, DynamicWindAPI::Handler::Push, DynamicWindAPI::Event::Update, position}};
                        std::array<DynamicWindAPI::Response, 1> responses;
                        WindProvider::Sample(frame, requests, responses);
                        if (responses[0].result == DynamicWindAPI::Result::Ready) {
                            const auto& wind = responses[0].velocity;
                            float scale = impulseScale * responses[0].pushSensitivity;
                            const float magnitude = std::hypot(wind.x, wind.y, wind.z);
                            if (responses[0].pushResponse == DynamicWindAPI::PushResponse::Cubic && magnitude > 0.0f) {
                                scale *= std::min(magnitude * magnitude, 1.0f / magnitude) /
                                    (DynamicWindAPI::PushReferenceStrength * DynamicWindAPI::PushReferenceStrength);
                            }
                            localVelocity = RE::hkVector4{wind.x * scale, wind.y * scale, wind.z * scale, 0.0f};
                        }
                    }
                }
                rigidBody->SetLinearImpulse(localVelocity);
            }
        }

        // If it's a NiNode recurse into children
        if (auto* niNode = node->AsNode()) {
            for (auto& child : niNode->GetChildren()) {
                ApplyImpulseToNode(child.get(), velocity, frame, formID, impulseScale);
            }
        }
    }

    // This is a holy grail function that allows us to replace the model of any reference with any model.
    // This is done by directly loading the NIF file of the new model and applying it to the reference.
    void ReplaceModel(RE::TESObjectREFR* ref, std::string modelPath) {
        SKSE::GetTaskInterface()->AddTask([ref, modelPath = std::move(modelPath)]() {
            if (!ref) return;
            if (!modelPath.ends_with(".nif")) {
                logger::error("Model file is not a NIF: {}", modelPath);
                return;
            }
            RE::NiPointer<RE::NiNode> newModel{nullptr};
            if (RE::BSResource::ErrorCode::kNone == RE::BSModelDB::Demand(modelPath.c_str(), newModel, {})) {
                RE::NiPointer<RE::NiObject> deepCopy;
                if (newModel && newModel.get()) {
                    newModel->CreateDeepCopy(deepCopy);
                }
                RE::NiAVObject* constructedObject = deepCopy ? deepCopy->AsNode() : nullptr;
                if (constructedObject) {
                    ref->Set3D(constructedObject, false);
                }
            } else {
                logger::error("Failed to load model for replacement");
            }
        });
    }

    void ReplaceBaseObject(RE::TESObjectREFR* ref, RE::TESBoundObject* newBase) {
        SKSE::GetTaskInterface()->AddTask([ref, newBase]() {
            if (!ref || !newBase) {
                logger::error("Invalid reference or base object for replacement");
                return;
            }
            ref->data.objectReference = newBase;
            ref->Disable();
            ref->Enable(false);
        });
    }

    std::string GetModelPath(RE::TESBoundObject* base) {
        if (!base) return "";

        if (auto activator = base->As<RE::TESObjectACTI>()) {
            return activator->model.c_str();
        }
        if (auto alch = base->As<RE::AlchemyItem>()) {
            return alch->model.c_str();
        }
        if (auto book = base->As<RE::TESObjectBOOK>()) {
            return book->model.c_str();
        }
        if (auto ingr = base->As<RE::IngredientItem>()) {
            return ingr->model.c_str();
        }
        if (auto light = base->As<RE::TESObjectLIGH>()) {
            return light->model.c_str();
        }
        if (auto misc = base->As<RE::TESObjectMISC>()) {
            return misc->model.c_str();
        }
        if (auto stat = base->As<RE::TESObjectSTAT>()) {
            return stat->model.c_str();
        }
        if (auto movStat = base->As<RE::BGSMovableStatic>()) {
            return movStat->model.c_str();
        }
        if (auto tree = base->As<RE::TESObjectTREE>()) {
            return tree->model.c_str();
        }
        if (auto flora = base->As<RE::TESFlora>()) {
            return flora->model.c_str();
        }
        if (auto furn = base->As<RE::TESFurniture>()) {
            return furn->model.c_str();
        }
        if (auto weap = base->As<RE::TESObjectWEAP>()) {
            return weap->model.c_str();
        }
        if (auto ammo = base->As<RE::TESAmmo>()) {
            return ammo->model.c_str();
        }
        if (auto cont = base->As<RE::TESObjectCONT>()) {
            return cont->model.c_str();
        }
        if (auto door = base->As<RE::TESObjectDOOR>()) {
            return door->model.c_str();
        }
        return "";
    }

    bool IsInWindCell(RE::TESObjectREFR* ref) {
        if (!ref) {
            return false;
        }

        RE::BGSLocation* worldspaceLocation{nullptr};
        RE::BGSLocation* cellLocation{nullptr};
        bool isInteriorCell{false};

        auto currentWorldspace = ref->GetWorldspace();
        if (currentWorldspace) {
            worldspaceLocation = currentWorldspace->location;
        }
        auto currentCell = ref->GetParentCell();
        if (currentCell) {
            cellLocation = currentCell->GetLocation();
            isInteriorCell = currentCell->IsInteriorCell();
        }

        bool isCaveWorldspace = worldspaceLocation ? worldspaceLocation->HasKeywordByEditorID("LocSetCave") ||
                                                         worldspaceLocation->HasKeywordByEditorID("LocSetCaveIce")
                                                   : false;
        
        // some cells in exterior worldspaces can have cave keyword, 
        // but they are not actual caves (e.g. cave entrances).
        // bool isCaveCell = cellLocation ? cellLocation->HasKeywordByEditorID("LocSetCave") ||
        //                                      cellLocation->HasKeywordByEditorID("LocSetCaveIce")
        //                                : false;

        if (isInteriorCell || isCaveWorldspace) {
            return false;
        }
        return true;
    }
}
        