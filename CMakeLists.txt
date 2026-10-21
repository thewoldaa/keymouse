# SPDX-License-Identifier: GPL-3.0-or-later
# CMake build for KeyMouse. See docs/BUILDING.md for prerequisites.

cmake_minimum_required(VERSION 3.24)

project(KeyMouse
    VERSION 0.3.3
    DESCRIPTION "Control the mouse entirely from the keyboard"
    HOMEPAGE_URL "https://github.com/thewoldaa/keymouse"
    LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR
        "KeyMouse currently targets Windows only. "
        "Linux and macOS backends are planned; see docs/PORTING.md.")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(KEYMOUSE_BUILD_TESTS "Build the unit test suite" ON)
option(KEYMOUSE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(KEYMOUSE_STATIC_RUNTIME "Link the MSVC runtime statically (/MT)" ON)

include(cmake/Dependencies.cmake)
include(cmake/CompilerWarnings.cmake)

# ---------------------------------------------------------------------------
# keymouse_core -- platform independent. Must not include <windows.h>.
# ---------------------------------------------------------------------------
add_library(keymouse_core STATIC
    src/core/action.cpp
    src/core/autoclick.cpp
    src/core/binding.cpp
    src/core/binding_table.cpp
    src/core/chord.cpp
    src/core/config.cpp
    src/core/engine.cpp
    src/core/i18n.cpp
    src/core/keycodes.cpp
    src/core/macro.cpp
    src/core/macro_store.cpp
    src/core/motion.cpp
    src/core/player.cpp
    src/core/profiles.cpp
    src/core/recorder.cpp
    src/core/router.cpp
    src/core/scheduler.cpp
)

target_include_directories(keymouse_core PUBLIC src)
target_link_libraries(keymouse_core PUBLIC nlohmann_json::nlohmann_json)
keymouse_set_warnings(keymouse_core)

# Single source for the version string used by the dashboard, --version and the
# diagnostics tab. PUBLIC because every other target links this one.
target_compile_definitions(keymouse_core PUBLIC
    KEYMOUSE_VERSION="${PROJECT_VERSION}")

# ---------------------------------------------------------------------------
# keymouse_win32 -- Windows input backend and platform services.
# ---------------------------------------------------------------------------
add_library(keymouse_win32 STATIC
    src/input/backend_win32.cpp
    src/input/keycodes_win32.cpp
    src/platform/beep_win32.cpp
    src/platform/dpi_win32.cpp
    src/platform/file_dialog_win32.cpp
    src/platform/locale_win32.cpp
    src/platform/mouse_hook_win32.cpp
    src/platform/paths_win32.cpp
    src/platform/screen_win32.cpp
    src/platform/session_win32.cpp
    src/platform/startup_win32.cpp
    src/platform/timer_win32.cpp
)

target_include_directories(keymouse_win32 PUBLIC src)
target_link_libraries(keymouse_win32 PUBLIC keymouse_core)
target_compile_definitions(keymouse_win32 PUBLIC
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)
target_link_libraries(keymouse_win32 PUBLIC
    user32
    shell32
    advapi32
    ole32
    dwmapi
    wtsapi32
    winmm
    comdlg32
)
keymouse_set_warnings(keymouse_win32)

# ---------------------------------------------------------------------------
# keymouse_ui -- Dear ImGui dashboard.
# ---------------------------------------------------------------------------
add_library(keymouse_ui STATIC
    src/ui/dashboard.cpp
    src/ui/fonts.cpp
    src/ui/theme.cpp
    src/ui/tray.cpp
    src/ui/widgets/chord_capture.cpp
    src/ui/tabs/tab_auto_click.cpp
    src/ui/tabs/tab_bindings.cpp
    src/ui/tabs/tab_diagnostics.cpp
    src/ui/tabs/tab_general.cpp
    src/ui/tabs/tab_macros.cpp
    src/ui/tabs/tab_mouse_mode.cpp
    src/ui/tabs/tab_movement.cpp
    src/ui/tabs/tab_profiles.cpp
    src/ui/tabs/tab_status.cpp
)

target_include_directories(keymouse_ui PUBLIC src)
target_link_libraries(keymouse_ui PUBLIC keymouse_core imgui_lib)
target_link_libraries(keymouse_ui PRIVATE keymouse_win32)
keymouse_set_warnings(keymouse_ui)

# ---------------------------------------------------------------------------
# keymouse -- the executable.
# ---------------------------------------------------------------------------
add_executable(keymouse
    src/main.cpp
    src/app/application.cpp
    src/app/cli.cpp
    src/app/single_instance.cpp
)

target_link_libraries(keymouse PRIVATE keymouse_ui keymouse_win32 keymouse_core)
keymouse_set_warnings(keymouse)

set_target_properties(keymouse PROPERTIES
    OUTPUT_NAME "KeyMouse"
    WIN32_EXECUTABLE ON
)

# Keep the translation files beside the binary so a plain build tree behaves
# like an installed one. The installer and the portable zip ship the same
# directory layout.
add_custom_command(TARGET keymouse POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/locales"
        "$<TARGET_FILE_DIR:keymouse>/locales"
    COMMENT "Copying locales next to the executable")

# Per-monitor DPI awareness and no elevation. The manifest is embedded rather
# than shipped as a sidecar so the portable zip is a single exe.
set(KEYMOUSE_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/resources/keymouse.manifest")
if(EXISTS "${KEYMOUSE_MANIFEST}")
    target_sources(keymouse PRIVATE "${KEYMOUSE_MANIFEST}")
    set_target_properties(keymouse PROPERTIES
        VS_DPI_AWARE "PerMonitor")
endif()

# Application icon and version block. The resource script is generated from a
# template so the version shown in the exe's properties cannot drift from
# project(VERSION ...) above.
if(MSVC)
    set(KEYMOUSE_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/resources/keymouse.ico")
    if(EXISTS "${KEYMOUSE_ICON_PATH}")
        string(REPLACE "." ","
            KEYMOUSE_VERSION_COMMA "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}.0")
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/keymouse.rc.in"
            "${CMAKE_CURRENT_BINARY_DIR}/keymouse.rc"
            @ONLY)
        target_sources(keymouse PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/keymouse.rc")
    else()
        message(WARNING
            "resources/keymouse.ico is missing; the build will have no icon. "
            "Regenerate it with: powershell -File tools/make_icon.ps1")
    endif()
endif()

if(KEYMOUSE_STATIC_RUNTIME)
    foreach(tgt keymouse keymouse_core keymouse_win32 keymouse_ui)
        set_property(TARGET ${tgt} PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endforeach()
endif()

# ---------------------------------------------------------------------------
# Install rules and packaging
# ---------------------------------------------------------------------------
# The portable zip uses the same layout the installer produces: the executable
# at the root, translations beside it, documentation in docs/. Nothing depends
# on an absolute path, so the folder can be moved anywhere afterwards.
install(TARGETS keymouse RUNTIME DESTINATION .)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/locales/" DESTINATION locales
        FILES_MATCHING PATTERN "*.json")
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/docs/" DESTINATION docs)
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/CHANGELOG.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    DESTINATION .)

set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_NAME "KeyMouse")
set(CPACK_PACKAGE_VENDOR "KeyMouse contributors")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
set(CPACK_PACKAGE_FILE_NAME "KeyMouse-${PROJECT_VERSION}-win64")
# Everything unpacks into KeyMouse-<version>-win64/ instead of spraying files
# into whatever directory the user happens to be in.
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(CPACK_VERBATIM_VARIABLES ON)
include(CPack)

if(KEYMOUSE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
