# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# SPDX-FileCopyrightText: 2018 yuzu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

# Credits to Samantas5855 and others for this function.
function(create_resource file output filename)
    # Read hex data from file
    file(READ ${file} filedata HEX)
    # Convert hex data for C compatibility
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," filedata ${filedata})
    # Write data to output file
    set(RESOURCES_DIR "${PROJECT_BINARY_DIR}/dist" PARENT_SCOPE)
    file(WRITE "${PROJECT_BINARY_DIR}/dist/${output}" "const unsigned char ${filename}[] = {${filedata}};\nconst unsigned ${filename}_size = sizeof(${filename});\n")
endfunction()

if (ENABLE_OPENGL)
    list(APPEND OPENGL_SOURCES
        emu_window/emu_window_sdl3_gl.cpp
        emu_window/emu_window_sdl3_gl.h
    )
else()
    set(OPENGL_SOURCES "")
endif()

add_executable(yuzu-cmd
    emu_window/emu_window_sdl3.cpp
    emu_window/emu_window_sdl3.h
    emu_window/emu_window_sdl3_null.cpp
    emu_window/emu_window_sdl3_null.h
    emu_window/emu_window_sdl3_vk.cpp
    emu_window/emu_window_sdl3_vk.h
    sdl_config.cpp
    sdl_config.h
    yuzu.cpp
    ${OPENGL_SOURCES}
)
if (WIN32)
    target_sources(yuzu-cmd PRIVATE yuzu.rc)
endif()

target_link_libraries(yuzu-cmd PRIVATE common core input_common frontend_common video_core)
if (ENABLE_OPENGL)
    target_link_libraries(yuzu-cmd PRIVATE glad)
endif()
target_link_libraries(yuzu-cmd PRIVATE ${PLATFORM_LIBRARIES} Threads::Threads)
if (NINTENDO_SWITCH2)
    target_sources(yuzu-cmd PRIVATE switch2_runtime.cpp switch2_launcher.cpp switch2_updater.cpp)
    target_link_libraries(yuzu-cmd PRIVATE dynarmic::dynarmic curl mbedtls mbedx509 mbedcrypto z)
    execute_process(
        COMMAND git rev-parse --short=7 HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE EDEN_NX2_GIT_SHA
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if (EDEN_NX2_GIT_SHA)
        target_compile_definitions(yuzu-cmd PRIVATE EDEN_NX2_GIT_SHA="${EDEN_NX2_GIT_SHA}")
    endif()
    set(EDEN_NX2_GIT_REF "$ENV{GITHUB_REF_NAME}")
    if (NOT EDEN_NX2_GIT_REF)
        execute_process(
            COMMAND git rev-parse --abbrev-ref HEAD
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE EDEN_NX2_GIT_REF
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
    endif()
    if (EDEN_NX2_GIT_REF STREQUAL "exp/switch2-ounce")
        target_compile_definitions(yuzu-cmd PRIVATE EDEN_NX2_UPDATER_CHANNEL_EXP=1)
    endif()
endif()

create_resource("../../dist/eden.bmp" "yuzu_cmd/yuzu_icon.h" "yuzu_icon")
target_include_directories(yuzu-cmd PRIVATE ${RESOURCES_DIR})

target_link_libraries(yuzu-cmd PRIVATE SDL3::SDL3)
target_link_libraries(yuzu-cmd PRIVATE OpenSSL::SSL OpenSSL::Crypto)

if(UNIX AND NOT APPLE)
    install(TARGETS yuzu-cmd)
endif()

if(WIN32)
    # compile as a win32 gui application instead of a console application
    if(MSVC)
        set_target_properties(yuzu-cmd PROPERTIES LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS")
    elseif(MINGW)
        set_target_properties(yuzu-cmd PROPERTIES LINK_FLAGS_RELEASE "-Wl,--subsystem,windows")
    endif()
endif()

create_target_directory_groups(yuzu-cmd)

# needed for vma
if (NOT MSVC)
    target_compile_options(yuzu-cmd PRIVATE
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-conversion>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-unused-variable>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-unused-parameter>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-missing-field-initializers>)
endif()
