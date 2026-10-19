// SPDX-License-Identifier: GPL-3.0-or-later
#include "test_common.h"

#include <thread>

#include "core/spsc_ring.h"

using namespace km;

TEST_CASE("push and pop preserve order") {
    SpscRing<int, 8> ring;
    for (int i = 0; i < 5; ++i) {
        CHECK(ring.push(i));
    }

    int value = 0;
    for (int i = 0; i < 5; ++i) {
        REQUIRE(ring.pop(value));
        CHECK(value == i);
    }
    CHECK_FALSE(ring.pop(value));
}

TEST_CASE("ring reports full at capacity") {
    SpscRing<int, 4> ring;  // holds 3 elements
    CHECK(ring.push(1));
    CHECK(ring.push(2));
    CHECK(ring.push(3));
    CHECK_FALSE(ring.push(4));
}

TEST_CASE("empty ring reports empty") {
    SpscRing<int, 8> ring;
    CHECK(ring.empty());
    ring.push(1);
    CHECK_FALSE(ring.empty());
}

TEST_CASE("size tracks occupancy") {
    SpscRing<int, 8> ring;
    CHECK(ring.size() == 0);
    ring.push(1);
    ring.push(2);
    CHECK(ring.size() == 2);

    int value = 0;
    ring.pop(value);
    CHECK(ring.size() == 1);
}

TEST_CASE("wraparound works") {
    SpscRing<int, 4> ring;
    int value = 0;

    for (int cycle = 0; cycle < 10; ++cycle) {
        CHECK(ring.push(cycle));
        CHECK(ring.push(cycle + 100));
        REQUIRE(ring.pop(value));
        CHECK(value == cycle);
        REQUIRE(ring.pop(value));
        CHECK(value == cycle + 100);
    }
}

TEST_CASE("concurrent producer and consumer lose nothing") {
    constexpr int kCount = 100000;
    SpscRing<int, 1024> ring;

    std::thread producer([&ring] {
        for (int i = 0; i < kCount; ++i) {
            while (!ring.push(i)) {
                std::this_thread::yield();
            }
        }
    });

    int expected = 0;
    int value = 0;
    while (expected < kCount) {
        if (ring.pop(value)) {
            REQUIRE(value == expected);
            ++expected;
        }
    }

    producer.join();
    CHECK(expected == kCount);
}

TEST_CASE("capacity reports usable slots") {
    CHECK(SpscRing<int, 8>::capacity() == 7);
    CHECK(SpscRing<int, 512>::capacity() == 511);
}
