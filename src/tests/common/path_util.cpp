// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "common/fs/path_util.h"

#ifndef _WIN32
#include <pwd.h>
#include <unistd.h>
#endif

#ifndef _WIN32
namespace {
/// Sets an environment variable for the test and puts the old value back after.
class ScopedEnv {
public:
    ScopedEnv(const char* name_, const char* value) : name{name_} {
        if (const char* old = std::getenv(name)) {
            original = old;
        }
        setenv(name, value, 1);
    }
    ~ScopedEnv() {
        if (original) {
            setenv(name, original->c_str(), 1);
        } else {
            unsetenv(name);
        }
    }

    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

private:
    const char* name;
    std::optional<std::string> original;
};
} // Anonymous namespace

// An empty or relative HOME would put every Eden directory under the working directory.
TEST_CASE("Common::FS::GetHomeDirectory ignores a HOME that is not absolute", "[common]") {
    const passwd* const entry = getpwuid(getuid());
    REQUIRE(entry != nullptr);
    for (const char* home : {"", "relative/home"}) {
        INFO("HOME=" << home);
        ScopedEnv env{"HOME", home};
        CHECK(Common::FS::GetHomeDirectory() == std::filesystem::path{entry->pw_dir});
    }
}

// The XDG Base Directory spec: an empty or relative value is ignored for the default under HOME.
TEST_CASE("Common::FS::GetDataDirectory ignores an XDG value that is not absolute", "[common]") {
    const auto home = Common::FS::GetHomeDirectory();
    for (const char* value : {"", "relative/data"}) {
        INFO("XDG_DATA_HOME=" << value);
        ScopedEnv env{"XDG_DATA_HOME", value};
        CHECK(Common::FS::GetDataDirectory("XDG_DATA_HOME") == home / ".local/share");
    }
}
#endif
