# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# SPDX-FileCopyrightText: 2023 yuzu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

add_library(frontend_common STATIC
    config.cpp
    firmware_manager.cpp
    data_manager.cpp
    settings_generator.cpp
    mod_manager.cpp
    update_checker.cpp)

target_sources(frontend_common PUBLIC
    FILE_SET frontendModules TYPE CXX_MODULES FILES
    settings_generator.ixx
    data_manager.ixx
    config.ixx
    content_manager.ixx
    firmware_manager.ixx
    mod_manager.ixx
    frontend.ixx
    update_checker.ixx)

create_target_directory_groups(frontend_common)
target_link_libraries(frontend_common
    PUBLIC core SimpleIni::SimpleIni frozen::frozen-headers
    PRIVATE common Boost::headers)
