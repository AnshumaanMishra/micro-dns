#include "../include/microdns/ring_buffer.hpp"

#include <gtest/gtest.h>

#include <thread>

TEST(RingBufferTest, SingleThreadPushPop) {
  LockFreeRingBuffer<int, 4> ring;
  int out_val = 0;

  EXPECT_FALSE(ring.pop(out_val));

  EXPECT_TRUE(ring.push(42));

  EXPECT_TRUE(ring.pop(out_val));
  EXPECT_EQ(out_val, 42);
}

TEST(RingBufferTest, CapacityAndFullState) {
  LockFreeRingBuffer<int, 4> ring;

  EXPECT_TRUE(ring.push(1));
  EXPECT_TRUE(ring.push(2));
  EXPECT_TRUE(ring.push(3));
  EXPECT_TRUE(ring.push(4));

  EXPECT_FALSE(ring.push(5));

  int out_val = 0;
  EXPECT_TRUE(ring.pop(out_val));
  EXPECT_EQ(out_val, 1);

  EXPECT_TRUE(ring.push(6));
}

TEST(RingBufferTest, MultiThreadedStressTest) {
  LockFreeRingBuffer<int, 256> ring;
  const int NUM_ITEMS = 10000;

  // Writer thread
  std::thread writer([&]() {
    for (int i = 0; i < NUM_ITEMS; ++i) {
      while (!ring.push(i)) {
        // Keep it running
      }
    }
  });

  // Reader thread
  std::thread reader([&]() {
    int val = 0;
    for (int i = 0; i < NUM_ITEMS; ++i) {
      while (!ring.pop(val)) {
        // Keep it running
      }
      EXPECT_EQ(val, i);
    }
  });

  writer.join();
  reader.join();
}