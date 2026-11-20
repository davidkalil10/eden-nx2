// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Modern Console / Handheld Console for Android: players assigned from the connected
// controllers. Everything here is kept in memory only (never SaveCurrentConfig), so the
// Manual players in the config are never changed.

#include <string>
#include <vector>

#include <jni.h>

#include "common/android/android_common.h"
#include "common/logging/log.h"
#include "common/param_package.h"
#include "common/settings.h"
#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/hid_core.h"
#include "input_common/main.h"
#include "native.h"

namespace {

using Core::HID::EmulatedController;
using Core::HID::NpadIdType;
using Core::HID::NpadStyleIndex;

// Clears a player's buttons and sticks, then maps the device's standard layout (none for
// an empty device: the touch overlay still reaches player 1 and the handheld)
void MapDevice(EmulatedController* controller, const Common::ParamPackage& device) {
    for (std::size_t i = 0; i < Settings::NativeButton::NumButtons; ++i) {
        controller->SetButtonParam(i, {});
    }
    for (std::size_t i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
        controller->SetStickParam(i, {});
    }
    if (!device.Has("engine")) {
        return;
    }
    auto& input = EmulationSession::GetInstance().GetInputSubsystem();
    const auto display = device.Get("display", "");
    for (auto [index, param] : input.GetButtonMappingForDevice(device)) {
        param.Set("display", display);
        controller->SetButtonParam(index, param);
    }
    for (auto [index, param] : input.GetAnalogMappingForDevice(device)) {
        param.Set("display", display);
        controller->SetStickParam(index, param);
    }
}

// A style the game takes for a player holding a whole controller
NpadStyleIndex PlayerStyle(const Core::HID::NpadStyleTag& styles) {
    if (styles.fullkey == 1) {
        return NpadStyleIndex::Fullkey;
    }
    if (styles.joycon_dual == 1) {
        return NpadStyleIndex::JoyconDual;
    }
    if (styles.gamecube == 1) {
        return NpadStyleIndex::GameCube;
    }
    return NpadStyleIndex::Fullkey;
}

void Set(EmulatedController* controller, const Common::ParamPackage& device,
         NpadStyleIndex style, bool connected) {
    controller->EnableConfiguration();
    if (connected) {
        MapDevice(controller, device);
    }
    controller->SetNpadStyleIndex(style);
    if (connected) {
        controller->Connect(true);
    } else {
        controller->Disconnect();
    }
    controller->DisableConfiguration();
}

} // namespace

extern "C" {

// devices: the controllers' param packages in player order (Kotlin orders them per mode).
// on_handheld: the only device is the handheld's own controls and plays as the handheld.
void Java_org_yuzu_yuzu_1emu_features_input_ConsoleMode_applyImpl(JNIEnv* env, jobject j_obj,
                                                                  jobjectArray j_devices,
                                                                  jboolean j_on_handheld) {
    std::vector<Common::ParamPackage> devices;
    const auto count = env->GetArrayLength(j_devices);
    for (jsize i = 0; i < count; ++i) {
        auto j_device = static_cast<jstring>(env->GetObjectArrayElement(j_devices, i));
        devices.emplace_back(Common::Android::GetJString(env, j_device));
        env->DeleteLocalRef(j_device);
    }

    auto& hid_core = EmulationSession::GetInstance().System().HIDCore();
    const auto styles = hid_core.GetSupportedStyleTag();
    const bool handheld_ok = styles.handheld == 1 && !Settings::IsDockedMode();
    auto* handheld = hid_core.GetEmulatedController(NpadIdType::Handheld);

    // Nothing connected, or only the handheld's own controls: play as the handheld (the
    // touch overlay follows player 1's style, so player 1 takes the handheld style too)
    if (handheld_ok && (devices.empty() || (devices.size() == 1 && j_on_handheld))) {
        const auto device = devices.empty() ? Common::ParamPackage{} : devices[0];
        Set(handheld, device, NpadStyleIndex::Handheld, true);
        for (std::size_t i = 0; i < 8; ++i) {
            auto* player = hid_core.GetEmulatedControllerByIndex(i);
            Set(player, {}, i == 0 ? NpadStyleIndex::Handheld : PlayerStyle(styles), false);
        }
        LOG_INFO(Input, "Console mode: handheld = {}",
                 devices.empty() ? "touch controls" : device.Get("display", "?"));
        return;
    }

    // Otherwise each controller is the next player
    Set(handheld, {}, NpadStyleIndex::Handheld, false);
    for (std::size_t i = 0; i < 8; ++i) {
        auto* player = hid_core.GetEmulatedControllerByIndex(i);
        // Player 1 stays connected with no controller: the touch overlay plays it
        const bool connected = i < devices.size() || i == 0;
        const auto device = i < devices.size() ? devices[i] : Common::ParamPackage{};
        Set(player, device, PlayerStyle(styles), connected);
        if (connected) {
            LOG_INFO(Input, "Console mode: player {} = {}", i + 1,
                     i < devices.size() ? device.Get("display", "?") : "touch controls");
        }
    }
}

} // extern "C"
