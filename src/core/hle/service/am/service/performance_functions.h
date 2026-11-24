// SPDX-FileCopyrightText: Copyright 2026 Eglantine Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <memory>

#include "core/hle/service/am/applet.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::AM {

// nn::am::service::IPerformanceFunctions (Switch 2), returned by IApplicationProxy command 40.
// Commands without a handler are auto-stubbed because the name carries the nx2: prefix.
class IPerformanceFunctions final : public ServiceFramework<IPerformanceFunctions> {
public:
    explicit IPerformanceFunctions(Core::System& system_, std::shared_ptr<Applet> applet)
        : ServiceFramework{system_, "nx2:IPerformanceFunctions"}, m_applet{std::move(applet)} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, D<&IPerformanceFunctions::IsCpuOverclockEnabled>, "IsCpuOverclockEnabled"},
            {1, D<&IPerformanceFunctions::SetCpuOverclockEnabled>, "SetCpuOverclockEnabled"},
        };
        // clang-format on
        RegisterHandlers(functions);
    }

private:
    Result IsCpuOverclockEnabled(Out<bool> out_is_enabled) {
        *out_is_enabled = m_cpu_overclock_requested.load();
        LOG_DEBUG(Service_AM, "IsCpuOverclockEnabled called, enabled={}", *out_is_enabled);
        R_SUCCEED();
    }

    Result SetCpuOverclockEnabled(bool enabled) {
        LOG_DEBUG(Service_AM, "SetCpuOverclockEnabled called, enabled={}", enabled);
        m_cpu_overclock_requested.store(enabled);
        R_SUCCEED();
    }

    const std::shared_ptr<Applet> m_applet;
    std::atomic<bool> m_cpu_overclock_requested{false};
};

} // namespace Service::AM
