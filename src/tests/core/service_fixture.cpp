// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <chrono>
#include <stdexcept>
#include <utility>

#include <cstdio>
#include <format>

#include "common/alignment.h"
#include "common/thread.h"
#include "core/core.h"
#include "core/file_sys/program_metadata.h"
#include "core/hle/kernel/k_client_session.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/kernel/k_handle_table.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_process_page_table.h"
#include "core/hle/kernel/k_resource_limit.h"
#include "core/hle/kernel/k_scoped_resource_reservation.h"
#include "core/hle/kernel/k_server_session.h"
#include "core/hle/kernel/k_session.h"
#include "core/hle/kernel/k_thread.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/result.h"
#include "core/hle/service/hle_ipc.h"
#include "core/hle/service/server_manager.h"
#include "core/memory.h"
#include "tests/core/kernel_fixture.h"
#include "tests/core/service_fixture.h"

namespace TestKernel {

namespace {

/// One page, so the address space has a code region to lay out around. Nothing is loaded into it.
constexpr std::size_t CodeSize = 0x1000;

/// Buffer descriptors carry byte addresses, but a service reading a struct out of one wants it
/// aligned the way the guest's allocator would have left it.
constexpr u64 ReservationAlignment = 16;

/// Big enough for every buffer this suite hands a service, and small next to the guest process
/// it hangs off.
constexpr u64 HarnessHeapSize = 8 * 1024 * 1024;

/// How long a test waits for a service worker before calling it a hang. The same patience
/// src/tests/core/async_context.cpp uses, and for the same reason: failing beats wedging the run.
constexpr auto Patience = std::chrono::seconds{5};

/// Throws rather than asserts, because the kernel's ASSERT only logs and carries on, and the test
/// binary starts no logging backend: a failed set-up would otherwise surface as an unexplained
/// zero address several frames later.
void Demand(Result result, std::string_view what) {
    if (result.IsError()) {
        throw std::runtime_error{std::format("{} failed with {:#010x}", what, result.raw)};
    }
}

} // Anonymous namespace

GuestProcess::GuestProcess(Kernel::KernelCore& kernel_, u64 heap_size) : kernel{kernel_} {
    process = Kernel::KProcess::Create(kernel);
    if (process == nullptr) {
        throw std::runtime_error{"KProcess::Create returned nothing"};
    }
    Kernel::KProcess::Register(kernel, process);

    Demand(process->LoadFromMetadata(kernel, FileSys::ProgramMetadata::GetDefault(), CodeSize, 0,
                                     0),
           "LoadFromMetadata");

    // The next two are the part of KProcess::Run this process will never reach, Run being private.
    // Without the handle table a reply carrying a handle writes into a table with no entries;
    // without a maximum, SetHeapSize refuses every size with ResultOutOfMemory, because the
    // maximum starts at zero and only Run derives one from the process memory limit.
    Demand(process->GetHandleTable().Initialize(
               kernel, static_cast<s32>(Kernel::KHandleTable::MaxTableSize)),
           "KHandleTable::Initialize");
    Demand(process->GetPageTable().SetMaxHeapSize(heap_size), "SetMaxHeapSize");

    Common::ProcessAddress address{};
    Demand(process->GetPageTable().SetHeapSize(std::addressof(address), heap_size), "SetHeapSize");

    heap_base = GetInteger(address);
    heap_end = heap_base + heap_size;
    next_free = heap_base;
}

GuestProcess::~GuestProcess() {
    // KProcess::Finalize only reaches the handle table through Exit(), and this process never ran,
    // so anything a reply left in it would keep its object alive past the kernel's shutdown check.
    process->GetHandleTable().Finalize(kernel);
    process->Close(kernel);
}

GuestProcess::Region GuestProcess::Reserve(u64 size) {
    const u64 address = Common::AlignUp(next_free, ReservationAlignment);
    next_free = address + size;
    if (next_free > heap_end) {
        throw std::runtime_error{"the guest heap is too small for this suite"};
    }

    // SetHeapSize does not promise zeroed pages, and a test reading back what a handler wrote has
    // to be able to tell a short write from a previous test's leftovers.
    process->GetMemory().ZeroBlock(address, size);

    return Region{address, size};
}

GuestProcess::Region GuestProcess::Write(std::span<const u8> bytes) {
    const Region region = Reserve(bytes.size());
    Write(region.address, bytes);
    return region;
}

void GuestProcess::Write(u64 address, std::span<const u8> bytes) {
    process->GetMemory().WriteBlock(address, bytes.data(), bytes.size());
}

std::vector<u8> GuestProcess::Read(u64 address, u64 size) const {
    std::vector<u8> bytes(size);
    process->GetMemory().ReadBlock(address, bytes.data(), bytes.size());
    return bytes;
}

ServiceHarness::ServiceHarness(Core::System& system_) : system{system_} {
    auto& kernel = system.Kernel();

    guest.emplace(kernel, HarnessHeapSize);

    // A ServerManager is only needed because SessionRequestManager cannot be built without one,
    // and because an interface returned on a plain session is registered through it. It is a real
    // one all the same, because its destructor waits for a LoopProcess that has run: handing it
    // to the kernel the way a service does means kernel shutdown is what retires it.
    struct Handover {
        Common::Event ready;
        Service::ServerManager* manager{};
    };
    auto handover = std::make_shared<Handover>();
    kernel
        .RunOnHostCoreProcess("hle-harness",
                              [&system_, handover] {
                                  auto manager = std::make_unique<Service::ServerManager>(system_);
                                  handover->manager = manager.get();
                                  handover->ready.Set();
                                  Service::ServerManager::RunServer(std::move(manager));
                              })
        .detach();
    handover->ready.Wait();
    server_manager = handover->manager;

    // The thread the whole harness runs on, owned by the guest process so that it is the current
    // process there. Built here rather than on the thread itself because the reservation and the
    // registration are the caller's job in the kernel's own RunHostThreadFunc too.
    Kernel::KScopedResourceReservation reservation{
        kernel, std::addressof(guest->Process()), Kernel::LimitableResource::ThreadCountMax};
    if (!reservation.Succeeded()) {
        throw std::runtime_error{"the guest process cannot afford another thread"};
    }

    host_thread = Kernel::KThread::Create(kernel);
    Demand(Kernel::KThread::InitializeDummyThread(system, host_thread,
                                                  std::addressof(guest->Process())),
           "InitializeDummyThread");
    reservation.Commit();
    Kernel::KThread::Register(kernel, host_thread);

    thread = std::thread{[this] { Run(); }};
}

ServiceHarness::~ServiceHarness() {
    // Nothing closed below throws today, but OnThread hands back whatever the job threw, and this
    // runs during static destruction, where an escaping exception is a terminate rather than a
    // failed test. Sessions a test left open are closed here, so this is not a rare path.
    try {
        for (std::size_t i = 0; i < sessions.size(); ++i) {
            Close(static_cast<SessionId>(i));
        }

        // The writable end belongs to whoever was handed the event, exactly as ServerManager's
        // destructor leaves it to ServiceManager.
        OnThread([this] {
            for (auto* event : deferral_events) {
                event->GetReadableEvent().Close(system.Kernel());
            }
        });
    } catch (const std::exception& e) {
        std::fprintf(stderr, "the service harness could not be taken down cleanly: %s\n", e.what());
    }

    {
        std::unique_lock lock{mutex};
        stopping = true;
        posted.notify_one();
    }
    thread.join();

    guest.reset();
}

void ServiceHarness::Run() {
    // The dummy thread has to be claimed from the thread that will use it, and only the first
    // caller on a host thread gets to choose: a later RegisterHostThread here would be a no-op
    // and every session would open a reference on the wrong process.
    system.Kernel().RegisterHostThread(host_thread);

    std::unique_lock lock{mutex};
    while (true) {
        posted.wait(lock, [this] { return busy || stopping; });
        if (!busy) {
            break;
        }

        auto job = std::move(work);
        lock.unlock();

        std::exception_ptr thrown;
        try {
            job();
        } catch (...) {
            thrown = std::current_exception();
        }

        lock.lock();
        failure = thrown;
        busy = false;
        finished.notify_one();
    }
    lock.unlock();

    // Frees the process reference the thread took, the way the kernel's own host threads end.
    host_thread->Close(system.Kernel());
}

void ServiceHarness::OnThread(const std::function<void()>& job) {
    std::unique_lock lock{mutex};
    work = job;
    busy = true;
    posted.notify_one();
    finished.wait(lock, [this] { return !busy; });

    if (failure) {
        std::rethrow_exception(std::exchange(failure, nullptr));
    }
}

ServiceHarness::OpenSession& ServiceHarness::Lookup(SessionId id) {
    const auto index = static_cast<std::size_t>(id);
    if (index >= sessions.size() || sessions[index].closed) {
        throw std::runtime_error{"the session is not open"};
    }
    return sessions[index];
}

SessionId ServiceHarness::Open(std::shared_ptr<Service::SessionRequestHandler> handler,
                               SessionKind kind) {
    OpenSession open;
    open.owns_session = true;
    open.object_id = kind == SessionKind::Domain ? 1 : 0;

    OnThread([this, &open, &handler, kind] {
        auto& kernel = system.Kernel();

        // KSession::PostDestroy releases this again, so skipping it would leave the guest
        // process's session count drifting down by one per test.
        if (!Kernel::GetCurrentProcess(kernel).GetResourceLimit()->Reserve(
                kernel, Kernel::LimitableResource::SessionCountMax, 1)) {
            throw std::runtime_error{"the guest process cannot afford another session"};
        }

        open.session = Kernel::KSession::Create(kernel);
        if (open.session == nullptr) {
            throw std::runtime_error{"KSession::Create returned nothing"};
        }
        open.session->Initialize(kernel, nullptr, 0);
        Kernel::KSession::Register(kernel, open.session);

        open.manager = std::make_shared<Service::SessionRequestManager>(kernel, *server_manager);
        open.manager->SetSessionHandler(std::move(handler));
        if (kind == SessionKind::Domain) {
            // Takes the handler that was just installed as domain object 1, which is what a guest
            // gets back from ConvertCurrentObjectToDomain.
            open.manager->ConvertToDomain();
        }
    });

    sessions.push_back(std::move(open));
    return static_cast<SessionId>(sessions.size() - 1);
}

SessionId ServiceHarness::OpenInterface(SessionId parent, const Reply& reply, u32 object_index) {
    const auto& source = Lookup(parent);

    OpenSession open;
    open.session = source.session;
    open.manager = source.manager;
    open.object_id = reply.DomainObject(object_index);

    sessions.push_back(std::move(open));
    return static_cast<SessionId>(sessions.size() - 1);
}

void ServiceHarness::Close(SessionId id) {
    const auto index = static_cast<std::size_t>(id);
    if (index >= sessions.size() || sessions[index].closed) {
        return;
    }

    auto& open = sessions[index];
    open.closed = true;

    OnThread([this, &open] {
        auto& kernel = system.Kernel();
        if (!open.owns_session) {
            // What CloseVirtualHandle does: the interface goes, the session it rode in on stays.
            open.manager->CloseDomainHandler(open.object_id - 1);
            return;
        }

        // Both ends, because KSession::Initialize leaves a reference for each and the process the
        // session opened is only released once they are both gone.
        open.session->GetClientSession().Close(kernel);
        open.session->GetServerSession().Close(kernel);
    });

    open.manager.reset();
}

Reply ServiceHarness::Call(SessionId id, const RequestBuilder& request) {
    auto& open = Lookup(id);
    auto buffer = request.Build(open.object_id);

    std::optional<Reply> reply;
    OnThread([this, &open, &buffer, &reply] {
        auto& kernel = system.Kernel();
        auto* server_session = std::addressof(open.session->GetServerSession());

        Service::HLERequestContext context{kernel, guest->Process().GetMemory(), server_session,
                                           host_thread};
        context.SetSessionRequestManager(open.manager);
        Demand(context.PopulateFromIncomingCommandBuffer(buffer.data()), "the request buffer");
        Demand(open.manager->CompleteSyncRequest(server_session, context), "the command");

        if (context.GetIsDeferred()) {
            throw std::runtime_error{"the handler parked the request; use CallDeferred"};
        }

        // ServiceFrameworkBase::HandleSyncRequest does this when the system is powered on, which
        // nothing here can do. It is not only the copy into the caller's thread-local region: the
        // ids of any interface the reply hands back are written by this call, not by the handler.
        Demand(context.WriteToOutgoingCommandBuffer(), "the reply buffer");

        reply.emplace(context.CommandBuffer(), context.GetDataPayloadOffset(),
                      open.manager->IsDomain());
    });

    return *reply;
}

DeferredReply ServiceHarness::CallDeferred(SessionId id, const RequestBuilder& request,
                                           Kernel::KEvent& deferral) {
    auto& open = Lookup(id);
    auto buffer = request.Build(open.object_id);

    std::optional<Reply> reply;
    u32 deferrals = 0;

    OnThread([this, &open, &buffer, &reply, &deferrals, &deferral] {
        auto& kernel = system.Kernel();
        auto* server_session = std::addressof(open.session->GetServerSession());

        Service::HLERequestContext context{kernel, guest->Process().GetMemory(), server_session,
                                           host_thread};
        context.SetSessionRequestManager(open.manager);
        Demand(context.PopulateFromIncomingCommandBuffer(buffer.data()), "the request buffer");

        while (true) {
            context.SetIsDeferred(false);
            Demand(open.manager->CompleteSyncRequest(server_session, context), "the command");
            if (!context.GetIsDeferred()) {
                break;
            }
            ++deferrals;

            // Clear doubles as the test for the signal: it refuses with ResultInvalidState while
            // the event is unsignalled and resets it under the same scheduler lock when it is, so
            // a signal cannot land between looking and clearing and be lost.
            const auto expiry = std::chrono::steady_clock::now() + Patience;
            while (deferral.Clear(kernel).IsError()) {
                if (std::chrono::steady_clock::now() > expiry) {
                    throw std::runtime_error{"nothing ever signalled the deferral event"};
                }
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }
        }

        Demand(context.WriteToOutgoingCommandBuffer(), "the reply buffer");
        reply.emplace(context.CommandBuffer(), context.GetDataPayloadOffset(),
                      open.manager->IsDomain());
    });

    return DeferredReply{*reply, deferrals};
}

Kernel::KEvent& ServiceHarness::MakeDeferralEvent() {
    Kernel::KEvent* event{};
    OnThread([this, &event] {
        auto& kernel = system.Kernel();
        event = Kernel::KEvent::Create(kernel);
        if (event == nullptr) {
            throw std::runtime_error{"KEvent::Create returned nothing"};
        }
        event->Initialize(kernel, nullptr);
        Kernel::KEvent::Register(kernel, event);
    });

    deferral_events.push_back(event);
    return *event;
}

ServiceHarness& Harness() {
    static ServiceHarness harness{Shared()};
    return harness;
}

} // namespace TestKernel
