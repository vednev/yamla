#include <catch2/catch_all.hpp>
#include "core/chunk_vector.hpp"
#include <vector>
#include <functional>

TEST_CASE("ChunkVector: empty", "[chunk_vector]") {
    ArenaChain chain;
    ChunkVector<int, 4> cv(chain);
    REQUIRE(cv.size() == 0);
    REQUIRE(cv.empty() == true);
    REQUIRE(cv.chunk_count() == 0);
}

TEST_CASE("ChunkVector: push_back + access", "[chunk_vector]") {
    ArenaChain chain;
    ChunkVector<int, 4> cv(chain);
    for (int i = 0; i < 10; ++i) cv.push_back(i);
    REQUIRE(cv.size() == 10);
    REQUIRE(cv[0] == 0);
    REQUIRE(cv[9] == 9);
}

TEST_CASE("ChunkVector: chunk boundaries", "[chunk_vector]") {
    ArenaChain chain;
    ChunkVector<int, 4> cv(chain);
    for (int i = 0; i < 8; ++i) cv.push_back(i);
    REQUIRE(cv.chunk_count() == 2);
}

TEST_CASE("ChunkVector: write through operator[]", "[chunk_vector]") {
    ArenaChain chain;
    ChunkVector<int, 4> cv(chain);
    for (int i = 0; i < 5; ++i) cv.push_back(i);
    cv[2] = 42;
    REQUIRE(cv[2] == 42);
}

TEST_CASE("ChunkVector: sort ascending", "[chunk_vector]") {
    ArenaChain chain;
    ArenaChain scratch;
    ChunkVector<int, 4> cv(chain);
    int vals[] = {5, 3, 8, 1, 4, 7, 2, 6};
    for (int v : vals) cv.push_back(v);
    cv.sort(scratch, std::less<int>{});
    int expected[] = {1, 2, 3, 4, 5, 6, 7, 8};
    for (int i = 0; i < 8; ++i) REQUIRE(cv[i] == expected[i]);
}

TEST_CASE("ChunkVector: sort descending", "[chunk_vector]") {
    ArenaChain chain;
    ArenaChain scratch;
    ChunkVector<int, 4> cv(chain);
    int vals[] = {5, 3, 8, 1, 4, 7, 2, 6};
    for (int v : vals) cv.push_back(v);
    cv.sort(scratch, std::greater<int>{});
    int expected[] = {8, 7, 6, 5, 4, 3, 2, 1};
    for (int i = 0; i < 8; ++i) REQUIRE(cv[i] == expected[i]);
}

TEST_CASE("ChunkVector: sort single element", "[chunk_vector]") {
    ArenaChain chain;
    ArenaChain scratch;
    ChunkVector<int, 4> cv(chain);
    cv.push_back(42);
    cv.sort(scratch, std::less<int>{});
    REQUIRE(cv[0] == 42);
}

TEST_CASE("ChunkVector: sort empty", "[chunk_vector]") {
    ArenaChain chain;
    ArenaChain scratch;
    ChunkVector<int, 4> cv(chain);
    cv.sort(scratch, std::less<int>{});
    REQUIRE(cv.size() == 0);
}

TEST_CASE("ChunkVector: iterator", "[chunk_vector]") {
    ArenaChain chain;
    ChunkVector<int, 4> cv(chain);
    for (int i = 0; i < 5; ++i) cv.push_back(i * 10);
    std::vector<int> out;
    for (auto& v : cv) out.push_back(v);
    REQUIRE(out == std::vector<int>{0, 10, 20, 30, 40});
}

TEST_CASE("ChunkVector: const iterator", "[chunk_vector]") {
    ArenaChain chain;
    ChunkVector<int, 4> cv(chain);
    for (int i = 0; i < 5; ++i) cv.push_back(i * 10);
    const auto& ccv = cv;
    std::vector<int> out;
    for (const auto& v : ccv) out.push_back(v);
    REQUIRE(out == std::vector<int>{0, 10, 20, 30, 40});
}

TEST_CASE("ChunkVector: clear", "[chunk_vector]") {
    ArenaChain chain;
    ChunkVector<int, 4> cv(chain);
    for (int i = 0; i < 10; ++i) cv.push_back(i);
    cv.clear();
    REQUIRE(cv.size() == 0);
    REQUIRE(cv.empty() == true);
}

TEST_CASE("ChunkVector: large dataset sort", "[chunk_vector]") {
    ArenaChain chain;
    ArenaChain scratch;
    ChunkVector<int> cv(chain);  // default CHUNK_CAPACITY = 65536
    constexpr int N = 100000;
    for (int i = N - 1; i >= 0; --i) cv.push_back(i);
    cv.sort(scratch, std::less<int>{});
    for (int i = 0; i < N; ++i) REQUIRE(cv[i] == i);
}
