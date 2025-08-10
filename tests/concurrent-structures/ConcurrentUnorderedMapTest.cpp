#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <random>
#include <ConcurrentUnorderedMap.h>

TEST(ConcurrentUnorderedMapTest, InsertAndGet) {
    ConcurrentUnorderedMap<int, std::string> map;

    map.InsertOrAssign(1, "one");
    auto value = map.Get(1);

    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value.value(), "one");
}

TEST(ConcurrentUnorderedMapTest, InsertOrAssignOverwrites) {
    ConcurrentUnorderedMap<int, std::string> map;

    map.InsertOrAssign(1, "one");
    map.InsertOrAssign(1, "uno");

    auto value = map.Get(1);
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value.value(), "uno");
}

TEST(ConcurrentUnorderedMapTest, EraseRemovesElement) {
    ConcurrentUnorderedMap<int, std::string> map;

    map.InsertOrAssign(1, "one");
    map.Erase(1);

    auto value = map.Get(1);
    EXPECT_FALSE(value.has_value());
}

TEST(ConcurrentUnorderedMapTest, SizeAndContains) {
    ConcurrentUnorderedMap<int, int> map;

    EXPECT_EQ(map.Size(), 0u);
    EXPECT_FALSE(map.Contains(1));

    map.InsertOrAssign(1, 42);
    EXPECT_EQ(map.Size(), 1u);
    EXPECT_TRUE(map.Contains(1));
}

TEST(ConcurrentUnorderedMapTest, GetNonExistent) {
    ConcurrentUnorderedMap<int, std::string> map;
    EXPECT_FALSE(map.Get(999).has_value());
}

TEST(ConcurrentUnorderedMapTest, ThreadSafetyBasic) {
    ConcurrentUnorderedMap<int, int> map;

    constexpr int THREADS = 8;
    constexpr int OPS = 1000;

    auto writer = [&map](int threadId) {
        for (int i = 0; i < OPS; ++i) {
            map.InsertOrAssign(threadId * OPS + i, i);
        }
    };

    std::vector<std::thread> threads;
    for (int t = 0; t < THREADS; ++t) {
        threads.emplace_back(writer, t);
    }
    for (auto &th : threads) {
        th.join();
    }

    EXPECT_EQ(map.Size(), THREADS * OPS);

    for (int i = 0; i < THREADS; ++i) {
        int key = i * OPS + (OPS / 2);
        auto value = map.Get(key);
        ASSERT_TRUE(value.has_value());
        EXPECT_EQ(value.value(), OPS / 2);
    }
}

TEST(ConcurrentUnorderedMapTest, MultiThread_MixedReadWriteErase) {
    ConcurrentUnorderedMap<int, int> map;

    constexpr int THREADS = 8;
    constexpr int OPS = 5000;
    std::atomic<bool> startFlag{false};

    auto worker = [&map, &startFlag](int id) {
        while (!startFlag.load()) {}
        std::mt19937 rng(id + 12345);
        std::uniform_int_distribution<int> keyDist(0, 2000);
        std::uniform_int_distribution<int> opDist(0, 2);

        for (int i = 0; i < OPS; ++i) {
            int key = keyDist(rng);
            int op = opDist(rng);
            if (op == 0) {
                map.InsertOrAssign(key, id);
            } else if (op == 1) {
                auto val = map.Get(key);
                if (val.has_value()) {
                    ASSERT_GE(val.value(), 0);
                }
            } else {
                map.Erase(key);
            }
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < THREADS; ++i)
        threads.emplace_back(worker, i);

    startFlag.store(true);
    for (auto &t : threads) t.join();

    EXPECT_LE(map.Size(), 2001u);
}

TEST(ConcurrentUnorderedMapTest, MultiThread_ReadHeavy) {
    ConcurrentUnorderedMap<int, int> map;
    constexpr int PRELOAD = 5000;
    for (int i = 0; i < PRELOAD; ++i)
        map.InsertOrAssign(i, i * 10);

    constexpr int THREADS = 8;
    constexpr int OPS = 10000;
    std::atomic<bool> startFlag{false};

    auto reader = [&map, &startFlag]() {
        while (!startFlag.load()) {}
        for (int i = 0; i < OPS; ++i) {
            auto val = map.Get(i % PRELOAD);
            ASSERT_TRUE(val.has_value());
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < THREADS; ++i)
        threads.emplace_back(reader);

    startFlag.store(true);
    for (auto &t : threads) t.join();
}

TEST(ConcurrentUnorderedMapTest, MultiThread_WriteHeavy) {
    ConcurrentUnorderedMap<int, int> map;
    constexpr int THREADS = 8;
    constexpr int OPS = 2000;
    std::atomic<bool> startFlag{false};

    auto writer = [&map, &startFlag](int id) {
        while (!startFlag.load()) {}
        for (int i = 0; i < OPS; ++i) {
            map.InsertOrAssign(id * OPS + i, i);
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < THREADS; ++i)
        threads.emplace_back(writer, i);

    startFlag.store(true);
    for (auto &t : threads) t.join();

    EXPECT_EQ(map.Size(), THREADS * OPS);
}

TEST(ConcurrentUnorderedMapTest, MultiThread_ContentionStress) {
    ConcurrentUnorderedMap<int, int> map;
    constexpr int THREADS = 12;
    constexpr int OPS = 3000;
    std::atomic<bool> startFlag{false};

    auto stressWorker = [&map, &startFlag](int id) {
        while (!startFlag.load()) {}
        std::mt19937 rng(id + 999);
        std::uniform_int_distribution<int> keyDist(0, 500);
        for (int i = 0; i < OPS; ++i) {
            int key = keyDist(rng);
            map.InsertOrAssign(key, id);
            auto v = map.Get(key);
            if (v.has_value()) {
                EXPECT_GE(v.value(), 0);
            }
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < THREADS; ++i)
        threads.emplace_back(stressWorker, i);

    startFlag.store(true);
    for (auto &t : threads) t.join();

    EXPECT_LE(map.Size(), 501u);
}
