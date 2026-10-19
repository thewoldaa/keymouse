// SPDX-License-Identifier: GPL-3.0-or-later

// This translation unit must not include test_common.h: the implementation
// macro has to be defined before doctest is first included, and test_common.h
// includes doctest.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
