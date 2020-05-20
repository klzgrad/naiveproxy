// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_one_block_arena.h"

#include <cstdint>

#include "net/third_party/quiche/src/quic/platform/api/quic_containers.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_expect_bug.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"

namespace quic {
namespace {

static const uint32_t kMaxAlign = 8;

struct TestObject {
  uint32_t value;
};

class QuicOneBlockArenaTest : public QuicTest {};

TEST_F(QuicOneBlockArenaTest, AllocateSuccess) {
  QuicOneBlockArena<1200> arena;
  QuicArenaScopedPtr<TestObject> ptr = arena.New<TestObject>();
  EXPECT_TRUE(ptr.is_from_arena());
}

TEST_F(QuicOneBlockArenaTest, Exhaust) {
  QuicOneBlockArena<1200> arena;
  for (size_t i = 0; i < 1200 / kMaxAlign; ++i) {
    QuicArenaScopedPtr<TestObject> ptr = arena.New<TestObject>();
    EXPECT_TRUE(ptr.is_from_arena());
  }
  QuicArenaScopedPtr<TestObject> ptr;
  EXPECT_QUIC_BUG(ptr = arena.New<TestObject>(),
                  "Ran out of space in QuicOneBlockArena");
  EXPECT_FALSE(ptr.is_from_arena());
}

TEST_F(QuicOneBlockArenaTest, NoOverlaps) {
  QuicOneBlockArena<1200> arena;
  std::vector<QuicArenaScopedPtr<TestObject>> objects;
  QuicIntervalSet<uintptr_t> used;
  for (size_t i = 0; i < 1200 / kMaxAlign; ++i) {
    QuicArenaScopedPtr<TestObject> ptr = arena.New<TestObject>();
    EXPECT_TRUE(ptr.is_from_arena());

    uintptr_t begin = reinterpret_cast<uintptr_t>(ptr.get());
    uintptr_t end = begin + sizeof(TestObject);
    EXPECT_FALSE(used.Contains(begin));
    EXPECT_FALSE(used.Contains(end - 1));
    used.Add(begin, end);
  }
}

}  // namespace
}  // namespace quic
