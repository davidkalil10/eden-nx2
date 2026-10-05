// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <cstring>
#include <memory>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <format>

#include "common/common_types.h"
#include "common/uuid.h"
#include "core/file_sys/fs_save_data_types.h"
#include "core/hle/result.h"
#include "core/hle/service/am/service/application_functions.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/service.h"
#include "tests/core/ipc_request.h"
#include "tests/core/kernel_fixture.h"
#include "tests/core/service_fixture.h"

namespace {

using Service::Out;

constexpr u32 ExtendSaveData = 25;
constexpr u32 GetSaveDataSize = 26;

/// The save folder a user's saves live in, as SaveDataFactory names it.
std::string FolderOf(const Common::UUID& user) {
    const auto raw = user.AsU128();
    return std::format("{:016X}{:016X}", raw[1], raw[0]);
}

/// Any user; the halves are distinct so a read at the wrong offset shows.
Common::UUID SomeUser() {
    const std::array<u64, 2> halves{0x0123456789ABCDEFULL, 0xFEDCBA9876543210ULL};
    std::array<u8, 16> bytes{};
    std::memcpy(bytes.data(), halves.data(), bytes.size());
    return Common::UUID{bytes};
}

/// What a game sends: the type, padding up to the 8-aligned user, then the sizes.
struct ExtendRequest {
    FileSys::SaveDataType type;
    std::array<u8, 7> padding;
    Common::UUID user;
    u64 normal_size;
    u64 journal_size;
};
static_assert(sizeof(ExtendRequest) == 0x28);

struct SizeRequest {
    FileSys::SaveDataType type;
    std::array<u8, 7> padding;
    Common::UUID user;
};
static_assert(sizeof(SizeRequest) == 0x18);

/// The two commands' argument lists, without the applet and save factory behind them.
class SaveDataSizeProbe final : public Service::ServiceFramework<SaveDataSizeProbe> {
public:
    explicit SaveDataSizeProbe(Core::System& system_)
        : ServiceFramework{system_, "save-size-probe"} {
        static const FunctionInfo functions[] = {
            {ExtendSaveData, D<&SaveDataSizeProbe::Extend>, "ExtendSaveData"},
            {GetSaveDataSize, D<&SaveDataSizeProbe::Size>, "GetSaveDataSize"},
        };
        RegisterHandlers(functions);
    }

    Common::UUID user{};
    FileSys::SaveDataType type{};
    u64 normal_size = 0;
    u64 journal_size = 0;

private:
    Result Extend(Out<u64> out_required_size, Service::AM::SaveDataSizeTarget target,
                  u64 normal_size_, u64 journal_size_) {
        type = target.type;
        user = target.user_id;
        normal_size = normal_size_;
        journal_size = journal_size_;
        *out_required_size = 0;
        R_SUCCEED();
    }

    Result Size(Out<u64> out_normal_size, Out<u64> out_journal_size,
                Service::AM::SaveDataSizeTarget target) {
        type = target.type;
        user = target.user_id;
        *out_normal_size = 0x1000;
        *out_journal_size = 0x2000;
        R_SUCCEED();
    }
};

struct Sent {
    std::shared_ptr<SaveDataSizeProbe> probe;
    TestKernel::Reply reply;
};

template <typename Request>
Sent Send(u32 command, const Request& request) {
    auto probe = std::make_shared<SaveDataSizeProbe>(TestKernel::Shared());
    auto& harness = TestKernel::Harness();
    const auto session = harness.Open(probe, TestKernel::SessionKind::Plain);
    TestKernel::RequestBuilder builder{command};
    builder.PushRaw(request);
    auto reply = harness.Call(session, builder);
    harness.Close(session);
    return {std::move(probe), std::move(reply)};
}

} // namespace

// The user follows the type 8-aligned; Common::UUID alone is 1-aligned and would be read from
// byte 1, seven bytes of padding and the first nine of the user.
TEST_CASE("am ExtendSaveData reads the user at byte 8", "[core][kernel]") {
    const ExtendRequest request{FileSys::SaveDataType::Account, {}, SomeUser(), 0x40000, 0x8000};
    auto sent = Send(ExtendSaveData, request);

    REQUIRE(sent.reply.GetResult() == ResultSuccess);
    REQUIRE(sent.reply.Pop<u64>() == 0);
    REQUIRE(sent.probe->type == FileSys::SaveDataType::Account);
    REQUIRE(FolderOf(sent.probe->user) == "FEDCBA98765432100123456789ABCDEF");
    REQUIRE(sent.probe->normal_size == 0x40000);
    REQUIRE(sent.probe->journal_size == 0x8000);
}

TEST_CASE("am GetSaveDataSize reads the user at byte 8", "[core][kernel]") {
    const SizeRequest request{FileSys::SaveDataType::Account, {}, SomeUser()};
    auto sent = Send(GetSaveDataSize, request);

    REQUIRE(sent.reply.GetResult() == ResultSuccess);
    REQUIRE(sent.reply.Pop<u64>() == 0x1000);
    REQUIRE(sent.reply.Pop<u64>() == 0x2000);
    REQUIRE(FolderOf(sent.probe->user) == "FEDCBA98765432100123456789ABCDEF");
}
