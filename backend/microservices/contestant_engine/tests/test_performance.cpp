#include <gtest/gtest.h>
#include "performance/object_pool.h"
#include "performance/lock_free_queue.h"
#include <thread>
#include <vector>

using namespace performance;

struct TestObject {
    int value = 0;
    std::string data;
};

TEST(ObjectPoolTest, AcquireAndRelease) {
    ObjectPool<TestObject> pool(10);
    
    auto obj = pool.acquire();
    ASSERT_NE(obj, nullptr);
    
    obj->value = 42;
    pool.release(std::move(obj));
    
    EXPECT_EQ(pool.size(), 10);
}

TEST(ObjectPoolTest, ReuseObjects) {
    ObjectPool<TestObject> pool(5);
    
    auto obj1 = pool.acquire();
    obj1->value = 100;
    auto* ptr1 = obj1.get();
    pool.release(std::move(obj1));
    
    auto obj2 = pool.acquire();
    EXPECT_EQ(obj2.get(), ptr1);
}

TEST(ObjectPoolTest, AutoGrowth) {
    ObjectPool<TestObject> pool(2);
    
    auto obj1 = pool.acquire();
    auto obj2 = pool.acquire();
    auto obj3 = pool.acquire();
    
    EXPECT_NE(obj1, nullptr);
    EXPECT_NE(obj2, nullptr);
    EXPECT_NE(obj3, nullptr);
}

TEST(ObjectPoolTest, MaxSizeEnforced) {
    ObjectPool<TestObject> pool(5);
    pool.setMaxSize(3);
    
    EXPECT_EQ(pool.size(), 3);
}

TEST(ObjectPoolTest, ThreadSafe) {
    ObjectPool<TestObject> pool(100);
    std::vector<std::thread> threads;
    
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([&pool]() {
            for (int j = 0; j < 100; ++j) {
                auto obj = pool.acquire();
                obj->value = j;
                pool.release(std::move(obj));
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_GT(pool.size(), 0);
}

TEST(LockFreeQueueTest, EnqueueDequeue) {
    LockFreeQueue<int> queue;
    
    queue.enqueue(42);
    queue.enqueue(100);
    
    auto val1 = queue.dequeue();
    auto val2 = queue.dequeue();
    
    ASSERT_TRUE(val1.has_value());
    ASSERT_TRUE(val2.has_value());
    EXPECT_EQ(*val1, 42);
    EXPECT_EQ(*val2, 100);
}

TEST(LockFreeQueueTest, EmptyQueue) {
    LockFreeQueue<int> queue;
    
    auto val = queue.dequeue();
    EXPECT_FALSE(val.has_value());
}

TEST(LockFreeQueueTest, IsEmpty) {
    LockFreeQueue<int> queue;
    
    EXPECT_TRUE(queue.empty());
    
    queue.enqueue(1);
    EXPECT_FALSE(queue.empty());
    
    queue.dequeue();
    EXPECT_TRUE(queue.empty());
}

TEST(LockFreeQueueTest, ThreadSafe) {
    LockFreeQueue<int> queue;
    std::atomic<int> enqueued{0};
    std::atomic<int> dequeued{0};
    
    std::thread producer([&]() {
        for (int i = 0; i < 1000; ++i) {
            queue.enqueue(i);
            enqueued.fetch_add(1);
        }
    });
    
    std::thread consumer([&]() {
        while (dequeued.load() < 1000) {
            if (auto val = queue.dequeue()) {
                dequeued.fetch_add(1);
            }
        }
    });
    
    producer.join();
    consumer.join();
    
    EXPECT_EQ(dequeued.load(), 1000);
}

TEST(LockFreeQueueTest, MultipleProducersConsumers) {
    LockFreeQueue<int> queue;
    std::atomic<int> total{0};
    
    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;
    
    for (int i = 0; i < 3; ++i) {
        producers.emplace_back([&queue]() {
            for (int j = 0; j < 100; ++j) {
                queue.enqueue(1);
            }
        });
    }
    
    for (int i = 0; i < 3; ++i) {
        consumers.emplace_back([&queue, &total]() {
            for (int j = 0; j < 100; ++j) {
                while (true) {
                    if (auto val = queue.dequeue()) {
                        total.fetch_add(*val);
                        break;
                    }
                }
            }
        });
    }
    
    for (auto& t : producers) t.join();
    for (auto& t : consumers) t.join();
    
    EXPECT_EQ(total.load(), 300);
}
