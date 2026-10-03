// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <filesystem>
#include <random>
#include <string>

#ifdef __linux__
#include <sys/inotify.h>
#include <unistd.h>
#endif

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

#ifdef __linux__
namespace {
/// The names @p fd reported deleted so far. The kernel queues an event before the call that
/// caused it returns, so nothing here waits.
std::vector<std::string> DeletedNames(int fd) {
    std::vector<std::string> out;
    alignas(inotify_event) char buffer[4096];
    for (ssize_t size; (size = read(fd, buffer, sizeof(buffer))) > 0;) {
        for (char* at = buffer; at < buffer + size;) {
            const auto* const event = reinterpret_cast<const inotify_event*>(at);
            if ((event->mask & IN_DELETE) != 0 && event->len > 0) {
                out.emplace_back(event->name);
            }
            at += sizeof(inotify_event) + event->len;
        }
    }
    return out;
}
} // namespace

// A manager constructed while another saves must still find profiles.dat, or it makes a default
// user and saves it over everyone.
TEST_CASE("Service::Account::ProfileManager profiles.dat is never missing while it is saved",
          "[core][acc]") {
    ScratchNand nand;
    Service::Account::ProfileManager manager; // a fresh NAND: saves its default user
    const auto dir = Common::FS::GetEdenPath(Common::FS::EdenPath::NANDDir) /
                     "system/save/8000000000000010/su/avators";
    REQUIRE(std::filesystem::is_regular_file(dir / "profiles.dat"));

    const int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    REQUIRE(fd >= 0);
    REQUIRE(inotify_add_watch(fd, dir.c_str(), IN_DELETE) >= 0);
    REQUIRE(manager.CreateNewUser(User(1), "One").IsSuccess());
    manager.WriteUserSaveFile();
    const auto deleted = DeletedNames(fd);
    close(fd);

    for (const auto& name : deleted) {
        CHECK(name != "profiles.dat");
    }
    Service::Account::ProfileManager reader;
    CHECK(reader.UserExists(User(1)));
}
#endif
