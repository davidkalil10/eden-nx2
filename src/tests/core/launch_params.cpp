// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <getopt.h>

#include "core/core.h"
#include "core/launch_params.h"
#include "tests/core/kernel_fixture.h"

namespace {

// getopt keeps its place in globals, and 0 asks glibc for a full restart of the scan.
Core::LaunchParams Parse(std::vector<std::string> args) {
    std::vector<char*> argv;
    for (auto& arg : args) {
        argv.push_back(arg.data());
    }
    argv.push_back(nullptr);
    optind = 0;
    return Core::ParseLaunchParams(TestKernel::Shared(), static_cast<int>(args.size()),
                                   argv.data(), nullptr);
}

} // namespace

// The switch reads optarg for -d and -i, so the option table has to promise them a value.
TEST_CASE("ParseLaunchParams: the options that take a value get it", "[frontend]") {
    const auto debug = Parse({"eden", "-d", "6543", "game.nro"});
    CHECK(debug.override_gdb_port == u16{6543});
    CHECK(debug.filepath == "game.nro");

    const auto profile = Parse({"eden", "--input-profile", "pad", "game.nro"});
    CHECK(profile.input_profile == "pad");
    CHECK(profile.filepath == "game.nro");

    // --program's value is read by hand from the next word, so the option must not swallow it.
    const auto program = Parse({"eden", "-p", "--flag", "game.nro"});
    CHECK(program.program_args == "--flag");
    CHECK(program.filepath == "game.nro");
}
