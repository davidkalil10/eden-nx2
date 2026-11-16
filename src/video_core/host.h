// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <functional>

#include "common/common_types.h"

namespace Core {
class DeviceMemory;
namespace Memory {
class Memory;
}
} // namespace Core

namespace Tegra {
class GPU;
namespace Host1x {
class Host1x;
}
} // namespace Tegra

namespace VideoCore {

/// What video_core needs from the emulated system, implemented by Core::System
class Host {
public:
    [[nodiscard]] virtual bool IsPoweredOn() const = 0;
    [[nodiscard]] virtual Tegra::GPU& GPU() = 0;
    [[nodiscard]] virtual Tegra::Host1x::Host1x& Host1x() = 0;
    [[nodiscard]] virtual Core::DeviceMemory& DeviceMemory() = 0;
    [[nodiscard]] virtual Core::Memory::Memory& ApplicationMemory() = 0;
    virtual void RegisterHostThread() = 0;
    virtual void GatherGPUDirtyMemory(std::function<void(PAddr, size_t)>& callback) = 0;
    [[nodiscard]] virtual u64 GPUTicks() = 0;
    virtual void EndGameFrame() = 0;

protected:
    ~Host() = default;
};

} // namespace VideoCore
