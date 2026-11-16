// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace Core::Timing {
class CoreTiming;
}

namespace Kernel {
class KernelCore;
}

namespace AudioCore {
class AudioCore;

/// What audio_core needs from the emulated system, implemented by Core::System
class Host {
public:
    [[nodiscard]] virtual bool IsPaused() const = 0;
    [[nodiscard]] virtual bool IsShuttingDown() const = 0;
    [[nodiscard]] virtual ::AudioCore::AudioCore& AudioCore() = 0;
    [[nodiscard]] virtual Core::Timing::CoreTiming& CoreTiming() = 0;
    [[nodiscard]] virtual Kernel::KernelCore& Kernel() = 0;

protected:
    ~Host() = default;
};

} // namespace AudioCore
