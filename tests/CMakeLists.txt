# SPDX-License-Identifier: GPL-3.0-or-later
include(FetchContent)

# doctest is header-only. Its own CMake target defines
# DOCTEST_CONFIG_IMPLEMENTATION_IN_DLL, which expects the implementation to
# live in a separate DLL; that conflicts with the standard
# IMPLEMENT_WITH_MAIN pattern used in test_main.cpp. Taking the headers
# directly avoids the mismatch.
FetchContent_Declare(doctest
    URL      https://github.com/doctest/doctest/archive/refs/tags/v2.4.12.tar.gz
    URL_HASH SHA256=73381c7aa4dee704bd935609668cf41880ea7f19fa0504a200e13b74999c2d70
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# Header-only consumption only, and no install rules: CPack would otherwise fill
# the portable zip with doctest's headers and CMake config files.
set(DOCTEST_NO_INSTALL ON CACHE INTERNAL "")
FetchContent_MakeAvailable(doctest)

add_executable(keymouse_tests
    test_main.cpp
    test_cli.cpp
    test_keycodes.cpp
    test_chord.cpp
    test_motion.cpp
    test_router.cpp
    test_config.cpp
    test_binding_table.cpp
    test_profiles.cpp
    test_scheduler.cpp
    test_engine.cpp
    test_macro.cpp
    test_ring.cpp
    # cli.cpp is in the application layer rather than in keymouse_core, but it
    # has no Windows dependency, so it can be tested here directly. Parsing
    # options in the abstract would test a copy of the logic instead of the real
    # thing.
    "${CMAKE_SOURCE_DIR}/src/app/cli.cpp"
)

target_include_directories(keymouse_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${doctest_SOURCE_DIR}
)

target_link_libraries(keymouse_tests PRIVATE keymouse_core)
# The scan code translation lives in the Win32 layer, which the core must not
# depend on. Tests run on Windows, so they may link it directly to verify the
# physical key mapping.
if(WIN32)
    target_link_libraries(keymouse_tests PRIVATE keymouse_win32)
endif()
keymouse_set_warnings(keymouse_tests)

if(KEYMOUSE_STATIC_RUNTIME)
    set_property(TARGET keymouse_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_test(NAME keymouse_tests COMMAND keymouse_tests)
