#include <catch2/catch_all.hpp>
#include "core/arena.hpp"
#include "core/arena_chain.hpp"
#include <cstring>

// ============================================================
//  ArenaAllocator tests
// ============================================================

TEST_CASE("Arena: basic allocation", "[arena]") {
    ArenaAllocator arena(4096);
    void* p = arena.alloc(64);
    REQUIRE(p != nullptr);
    REQUIRE(arena.used() >= 64);
}

TEST_CASE("Arena: alignment", "[arena]") {
    ArenaAllocator arena(4096);
    arena.alloc(1, 1);  // 1-byte alloc, alignment 1
    void* p = arena.alloc(8, 8);  // 8-byte alloc, alignment 8
    REQUIRE(reinterpret_cast<uintptr_t>(p) % 8 == 0);
}

TEST_CASE("Arena: alloc_zeroed", "[arena]") {
    ArenaAllocator arena(4096);
    void* p = arena.alloc_zeroed(128);
    REQUIRE(p != nullptr);
    char zeros[128] = {};
    REQUIRE(std::memcmp(p, zeros, 128) == 0);
}

TEST_CASE("Arena: alloc_one and alloc_array", "[arena]") {
    ArenaAllocator arena(4096);

    int64_t* one = arena.alloc_one<int64_t>();
    REQUIRE(one != nullptr);
    *one = 42;
    REQUIRE(*one == 42);

    int32_t* arr = arena.alloc_array<int32_t>(10);
    REQUIRE(arr != nullptr);
    for (int i = 0; i < 10; ++i) arr[i] = i * 100;
    for (int i = 0; i < 10; ++i) REQUIRE(arr[i] == i * 100);
}

TEST_CASE("Arena: intern_string", "[arena]") {
    ArenaAllocator arena(4096);
    const char* src = "hello";
    const char* interned = arena.intern_string(src, 5);
    REQUIRE(std::strcmp(interned, "hello") == 0);
    // Must be a copy (different address)
    REQUIRE(interned != src);
}

TEST_CASE("Arena: reset", "[arena]") {
    ArenaAllocator arena(4096);
    arena.alloc(512);
    REQUIRE(arena.used() >= 512);
    arena.reset();
    REQUIRE(arena.used() == 0);
}

TEST_CASE("Arena: capacity and remaining", "[arena]") {
    ArenaAllocator arena(4096);
    arena.alloc(100);
    REQUIRE(arena.remaining() == arena.capacity() - arena.used());
}

// ============================================================
//  ArenaChain tests
// ============================================================

TEST_CASE("ArenaChain: starts with one slab", "[arena_chain]") {
    ArenaChain chain;
    REQUIRE(chain.slab_count() == 1);
}

TEST_CASE("ArenaChain: basic allocation", "[arena_chain]") {
    ArenaChain chain;
    void* p = chain.alloc(1024);
    REQUIRE(p != nullptr);
}

TEST_CASE("ArenaChain: intern_string stability", "[arena_chain]") {
    ArenaChain chain;
    const char* s1 = chain.intern_string("hello", 5);
    const char* s2 = chain.intern_string("world", 5);
    REQUIRE(std::strcmp(s1, "hello") == 0);
    REQUIRE(std::strcmp(s2, "world") == 0);
}

TEST_CASE("ArenaChain: reset", "[arena_chain]") {
    ArenaChain chain;
    chain.alloc(1024);
    chain.reset();
    REQUIRE(chain.slab_count() == 1);
    REQUIRE(chain.approximate_used() == 0);
}

TEST_CASE("ArenaChain: alloc_array", "[arena_chain]") {
    ArenaChain chain;
    uint64_t* arr = chain.alloc_array<uint64_t>(100);
    REQUIRE(arr != nullptr);
    for (int i = 0; i < 100; ++i) arr[i] = static_cast<uint64_t>(i);
    for (int i = 0; i < 100; ++i) REQUIRE(arr[i] == static_cast<uint64_t>(i));
}

TEST_CASE("ArenaChain: multi-slab growth", "[arena_chain]") {
    ArenaChain chain;

    // Each slab is 256MB. Allocate 1MB blocks until we get > 1 slab.
    constexpr size_t block = 1024 * 1024; // 1 MB
    // First, grab a pointer from the first allocation
    uint8_t* first_ptr = static_cast<uint8_t*>(chain.alloc(block));
    first_ptr[0] = 0xAB;

    while (chain.slab_count() <= 1) {
        chain.alloc(block);
    }
    REQUIRE(chain.slab_count() > 1);

    // Earlier pointer still readable
    REQUIRE(first_ptr[0] == 0xAB);
}
