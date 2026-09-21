// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "common/common_types.h"
#include "tests/core/ipc_request.h"

namespace Core {
class System;
}

namespace Kernel {
class KernelCore;
class KEvent;
class KProcess;
class KSession;
class KThread;
} // namespace Kernel

namespace Service {
class ServerManager;
class SessionRequestHandler;
class SessionRequestManager;
} // namespace Service

namespace TestKernel {

/**
 * A guest process with a heap, built without a ROM.
 *
 * An HLE handler reads and writes the buffers a request names through the calling process's own
 * page table, so anything that hands a service a buffer address needs a process with mapped
 * pages. Neither process the kernel already has will do: the ones behind
 * KernelHelpers::ServiceContext and RunOnHostCoreProcess are made with the KProcess::Initialize
 * overload that builds no page table, so every address in them is unmapped.
 *
 * LoadFromMetadata is the overload that does build one, and ProgramMetadata::GetDefault()
 * describes a 64-bit application without reading a file, which is what the loaders pass for an
 * NRO. What it leaves out is the part KProcess::Run does, and Run is private: the handle table
 * has to be initialized here or a reply carrying a handle writes into a table with no entries.
 *
 * Expensive to build, because LoadFromMetadata also creates four Dynarmic interfaces
 * (KProcess::InitializeInterfaces). One per suite, like the kernel it lives in.
 */
class GuestProcess {
public:
    /// An address and a length inside the heap: what a buffer descriptor is built from.
    struct Region {
        u64 address;
        u64 size;
    };

    explicit GuestProcess(Kernel::KernelCore& kernel_, u64 heap_size);
    ~GuestProcess();

    GuestProcess(const GuestProcess&) = delete;
    GuestProcess& operator=(const GuestProcess&) = delete;

    [[nodiscard]] Kernel::KProcess& Process() const {
        return *process;
    }

    /// Hands out @p size zeroed bytes of heap nothing else here is using.
    [[nodiscard]] Region Reserve(u64 size);

    /// Reserve plus a write, for the input side of a request.
    Region Write(std::span<const u8> bytes);

    void Write(u64 address, std::span<const u8> bytes);

    [[nodiscard]] std::vector<u8> Read(u64 address, u64 size) const;

private:
    Kernel::KernelCore& kernel;
    Kernel::KProcess* process{};
    u64 heap_base{};
    u64 heap_end{};
    u64 next_free{};
};

/// How a session presents itself to the handler behind it.
enum class SessionKind {
    /// What sm: hands out. A command that returns an interface moves a handle.
    Plain,
    /// What a guest converts to before asking for sub-interfaces, and the only kind a test can
    /// follow an interface through: a moved handle would have to be read back out of a handle
    /// table by a guest thread, and there is no guest thread here.
    Domain,
};

/// A session the harness has open. The kernel objects behind it belong to the harness.
enum class SessionId : std::size_t {};

/// A reply the handler only produced after parking the request at least once.
struct DeferredReply {
    Reply reply;
    /// How many times the handler parked it before answering.
    u32 deferrals;
};

/**
 * Enough of the HLE transport to invoke a service command and read its reply.
 *
 * Not the real transport: driving a service the way a game does needs a full boot, for three
 * independent reasons. The reply is written to the calling thread's thread-local region, which
 * only a ThreadType::User thread has; a client thread cannot block in SendSyncRequest from a host
 * thread; and ServiceFrameworkBase::HandleSyncRequest skips writing the reply out unless the
 * system is powered on. What is left once those are dropped is still the whole of the command
 * path: the request buffer is parsed by the real HLERequestContext, the real
 * SessionRequestManager dispatches it, and the handler answers through the real ResponseBuilder.
 *
 * The service manager is the one piece a test can have in full, over
 * Core::System::InitializeServiceManager, so a module that only exists inside its own .cpp is
 * still reachable by the name it registers. StartModule and OpenNamed are that route.
 *
 * Everything here runs on one thread of its own, and it must. Registering a host thread with the
 * kernel is one-shot, and the main thread was already claimed during KernelCore::Initialize with
 * a dummy thread that has no owner process, so KSession::Initialize would open a reference on a
 * null process from a test's thread.
 *
 * Shared for the same reason the kernel is: the guest process behind it is expensive, and the
 * kernel does not give back what a second one would take. Close what you open, or the session
 * keeps the guest process alive past the end of the suite.
 */
class ServiceHarness {
public:
    explicit ServiceHarness(Core::System& system_);
    ~ServiceHarness();

    ServiceHarness(const ServiceHarness&) = delete;
    ServiceHarness& operator=(const ServiceHarness&) = delete;

    /// The one process every session here speaks for, and the only memory a buffer can live in.
    [[nodiscard]] GuestProcess& Process() {
        return *guest;
    }

    /// A session onto @p handler, which the session keeps alive.
    [[nodiscard]] SessionId Open(std::shared_ptr<Service::SessionRequestHandler> handler,
                                 SessionKind kind);

    /**
     * Runs a service module's LoopProcess on a host thread of its own, as Service::Services does.
     *
     * The way to reach a service whose class has no header: it registers itself by name on the
     * way in, and OpenNamed asks the service manager for it afterwards. Calling this twice for
     * the same module does nothing. The filesystem factories are not created: see StartModule.
     *
     * @p names is every service the module registers, not only the one a test means to open. The
     * harness closes each of their ports itself, in the destructor: the service manager belongs to
     * System and is destroyed after KernelCore::Shutdown, where the last reference to a port
     * unregisters itself from an object container the kernel has already destroyed.
     */
    void StartModule(void (*loop_process)(Core::System&),
                     std::span<const std::string_view> names);

    /// A session onto the service registered as @p name, the handler sm: would hand a guest.
    [[nodiscard]] SessionId OpenNamed(std::string_view name, SessionKind kind);

    /// An interface a domain command handed back, addressable as a session of its own.
    [[nodiscard]] SessionId OpenInterface(SessionId parent, const Reply& reply,
                                          u32 object_index = 0);

    void Close(SessionId id);

    /// One request in, the handler's reply out. Fails if the handler parked the request.
    [[nodiscard]] Reply Call(SessionId id, const RequestBuilder& request);

    /**
     * A request the handler is expected to park, and answer when @p deferral is signalled.
     *
     * The same loop ServerManager runs: clear the deferred flag, call the handler, and if it set
     * the flag again, wait for the deferral event and call it once more with the very same
     * context, buffers and all. Fails rather than hangs if nothing ever signals.
     */
    [[nodiscard]] DeferredReply CallDeferred(SessionId id, const RequestBuilder& request,
                                             Kernel::KEvent& deferral);

    /**
     * An event to hand a module as its deferral event, as ServerManager::ManageDeferral makes one.
     *
     * Ownership splits the same way it does there: whoever is handed this closes the writable
     * end, and the harness closes the readable end.
     */
    [[nodiscard]] Kernel::KEvent& MakeDeferralEvent();

private:
    /// One addressable endpoint. A domain's sub-interfaces share their parent's kernel session.
    struct OpenSession {
        Kernel::KSession* session{};
        std::shared_ptr<Service::SessionRequestManager> manager;
        u32 object_id{};
        bool owns_session{};
        bool closed{};
    };

    void Run();
    void OnThread(const std::function<void()>& job);
    [[nodiscard]] OpenSession& Lookup(SessionId id);

    Core::System& system;
    std::optional<GuestProcess> guest;
    Service::ServerManager* server_manager{};
    Kernel::KThread* host_thread{};
    std::vector<OpenSession> sessions;
    std::vector<Kernel::KEvent*> deferral_events;
    std::vector<void (*)(Core::System&)> modules;
    std::vector<std::string> module_services;

    std::mutex mutex;
    std::condition_variable posted;
    std::condition_variable finished;
    std::function<void()> work;
    std::exception_ptr failure;
    bool busy{false};
    bool stopping{false};
    std::thread thread;
};

/**
 * The harness this suite shares, brought up on first use over TestKernel::Shared().
 *
 * Torn down before the kernel is, because it is built after it: its sessions, its guest process
 * and its server thread all have to be gone while the kernel can still retire them.
 */
[[nodiscard]] ServiceHarness& Harness();

} // namespace TestKernel
