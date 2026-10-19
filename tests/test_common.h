// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Shared prelude for every test translation unit.
//
// doctest stringifies types by streaming them, which requires <ostream> to be
// visible. Including it here keeps the test files from each repeating the
// include order that makes that work.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include <doctest/doctest.h>
