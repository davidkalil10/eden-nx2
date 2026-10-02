// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>

#include "core/hle/service/os/multi_wait_holder.h"

namespace Kernel {
class KernelCore;
class KProcess;
} // namespace Kernel

namespace Service {
class Process;
}

namespace Service::AM {

struct Applet;

class ProcessHolder : public MultiWaitHolder, public Common::IntrusiveListBaseNode<ProcessHolder> {
public:
    /// Waits on @p watched, the applet's process when the holder is made, and keeps it open.
    ProcessHolder(Kernel::KernelCore& kernel, Kernel::KProcess* watched,
                  std::shared_ptr<Applet> applet);
    ~ProcessHolder();

    const std::shared_ptr<Applet>& GetApplet() const {
        return m_applet;
    }

    Process& GetProcess() const;

    Kernel::KProcess* GetWatchedProcess() const {
        return m_watched;
    }

private:
    Kernel::KernelCore& m_kernel;
    // Opened: winding restarts an applet in a new process and can close the old one before the
    // observer handles its end.
    Kernel::KProcess* m_watched;
    // Owned: the window system drops an applet when its process ends, which can come before the
    // observer handles that end.
    std::shared_ptr<Applet> m_applet;
};

} // namespace Service::AM
