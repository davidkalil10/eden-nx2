// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/kernel/k_process.h"
#include "core/hle/service/am/applet.h"
#include "core/hle/service/am/process_holder.h"
#include "core/hle/service/os/process.h"

namespace Service::AM {

ProcessHolder::ProcessHolder(Kernel::KernelCore& kernel, Kernel::KProcess* watched,
                             std::shared_ptr<Applet> applet)
    : MultiWaitHolder(watched), m_kernel(kernel), m_watched(watched),
      m_applet(std::move(applet)) {
    m_watched->Open(m_kernel);
}

ProcessHolder::~ProcessHolder() {
    m_watched->Close(m_kernel);
}

Process& ProcessHolder::GetProcess() const {
    return *m_applet->process;
}

} // namespace Service::AM
