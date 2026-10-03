// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <filesystem>
#include <random>

#include <catch2/catch_test_macros.hpp>
#include <format>

#include "common/fs/path_util.h"
#include "core/hle/service/acc/profile_manager.h"

namespace {

/// A NAND of its own: the manager reads and writes the profiles file there.
class ScratchNand {
public:
    ScratchNand()
        : saved{Common::FS::GetEdenPath(Common::FS::EdenPath::NANDDir)},
          dir{std::filesystem::temp_directory_path() /
              std::format("eden-profile-manager-{:08x}", std::random_device{}())} {
        std::filesystem::create_directories(dir);
        Common::FS::SetEdenPath(Common::FS::EdenPath::NANDDir, dir);
    }
    ~ScratchNand() {
        Common::FS::SetEdenPath(Common::FS::EdenPath::NANDDir, saved);
        std::error_code ignored;
        std::filesystem::remove_all(dir, ignored);
    }

private:
    std::filesystem::path saved;
    std::filesystem::path dir;
};

Common::UUID User(u8 n) {
    return Common::UUID{std::array<u8, 16>{0x9a, n, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}};
}

} // namespace

// A removed user gives its place back: the next one takes the next slot, and removing and adding
// over and over never runs out of places.
TEST_CASE("Service::Account::ProfileManager a removed user gives its place back", "[core][acc]") {
    ScratchNand nand;
    Service::Account::ProfileManager manager;
    const auto start = manager.GetUserCount(); // the default user of a fresh NAND
    REQUIRE(manager.CreateNewUser(User(1), "One").IsSuccess());
    REQUIRE(manager.CreateNewUser(User(2), "Two").IsSuccess());
    REQUIRE(manager.GetUserCount() == start + 2);

    REQUIRE(manager.RemoveUser(User(1)));
    CHECK(manager.GetUserCount() == start + 1);
    CHECK_FALSE(manager.UserExists(User(1)));

    // The next user takes the next place, with no empty one before it.
    REQUIRE(manager.CreateNewUser(User(3), "Three").IsSuccess());
    CHECK(manager.GetUserCount() == start + 2);
    for (std::size_t index = 0; index < manager.GetUserCount(); ++index) {
        INFO(index);
        CHECK(manager.GetUser(index).value_or(Common::UUID{}).IsValid());
    }

    // Removing and adding over and over never runs out of places.
    for (u8 n = 10; n < 30; ++n) {
        REQUIRE(manager.CreateNewUser(User(n), "Again").IsSuccess());
        REQUIRE(manager.RemoveUser(User(n)));
    }
    CHECK(manager.GetUserCount() == start + 2);
    CHECK(manager.CanSystemRegisterUser());
}
