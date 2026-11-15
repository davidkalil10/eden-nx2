// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <vector>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include "common/settings_setting.h"

namespace Settings {

TEMPLATE_TEST_CASE("Setting callbacks", "[common]", (Setting<int, true>),
                   (SwitchableSetting<int, true>)) {
    Linkage linkage;
    TestType setting{linkage, 3, 0, 10, "test", Category::Core};
    std::vector<int> values;

    setting.SetValue(4);
    setting.RegisterCallback([&](const int& value) { values.push_back(value); });
    REQUIRE(values == std::vector<int>{4});

    setting.SetValue(20);
    setting = 6;
    REQUIRE(values == std::vector<int>{4, 10, 6});

    setting.RegisterCallback({});
    setting.SetValue(2);
    REQUIRE(values == std::vector<int>{4, 10, 6});
}

TEST_CASE("Setting callbacks follow global and custom values", "[common]") {
    Linkage linkage;
    SwitchableSetting<int> setting{linkage, 3, "test", Category::Core};
    std::vector<int> values;

    setting.RegisterCallback([&](const int& value) { values.push_back(value); });
    setting.SetGlobal(false);
    setting.SetValue(7);
    setting.SetGlobal(true);
    setting.SetGlobal(true);
    REQUIRE(values == std::vector<int>{3, 0, 7, 3});
}

} // namespace Settings
