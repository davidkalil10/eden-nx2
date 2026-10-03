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

// A sibling whose name starts with the base's is outside it, so ConcatPathSafe keeps "../nandx"
// in the NAND.
TEST_CASE("Common::FS::IsPathSandboxed compares whole names", "[common]") {
    using Common::FS::ConcatPathSafe;
    using Common::FS::IsPathSandboxed;
    CHECK(IsPathSandboxed("/eden/nand", "/eden/nand"));
    CHECK(IsPathSandboxed("/eden/nand", "/eden/nand/user/save"));
    CHECK(IsPathSandboxed("/eden/nand/", "/eden/nand/user"));
    CHECK_FALSE(IsPathSandboxed("/eden/nand", "/eden/nandx"));
    CHECK_FALSE(IsPathSandboxed("/eden/nand", "/eden/nandx/user"));
    CHECK(ConcatPathSafe("/eden/nand", "../nandx/user") == std::filesystem::path{"/eden/nand"});
}

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

// An empty HOME made every Eden directory relative, so the data went wherever the emulator was
// started from.
TEST_CASE("Common::FS::GetHomeDirectory ignores a HOME that is not absolute", "[common]") {
    const passwd* const entry = getpwuid(getuid());
    REQUIRE(entry != nullptr);
    for (const char* home : {"", "relative/home"}) {
        INFO("HOME=" << home);
        ScopedEnv env{"HOME", home};
        CHECK(Common::FS::GetHomeDirectory() == std::filesystem::path{entry->pw_dir});
    }
}

// XDG_DATA_HOME set but empty put the data in "eden" under the working directory. The spec says
// to ignore an empty or relative value and use the default under HOME.
TEST_CASE("Common::FS::GetDataDirectory ignores an XDG value that is not absolute", "[common]") {
    const auto home = Common::FS::GetHomeDirectory();
    for (const char* value : {"", "relative/data"}) {
        INFO("XDG_DATA_HOME=" << value);
        ScopedEnv env{"XDG_DATA_HOME", value};
        CHECK(Common::FS::GetDataDirectory("XDG_DATA_HOME") == home / ".local/share");
    }
}
#endif
