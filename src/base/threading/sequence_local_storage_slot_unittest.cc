// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/threading/sequence_local_storage_slot.h"

#include <utility>

#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/threading/sequence_local_storage_map.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {

namespace {

class SequenceLocalStorageSlotTest : public testing::Test {
 protected:
  SequenceLocalStorageSlotTest()
      : scoped_sequence_local_storage_(&sequence_local_storage_) {}

  internal::SequenceLocalStorageMap sequence_local_storage_;
  internal::ScopedSetSequenceLocalStorageMapForCurrentThread
      scoped_sequence_local_storage_;

 private:
  DISALLOW_COPY_AND_ASSIGN(SequenceLocalStorageSlotTest);
};

}  // namespace

// Verify that a value stored with Set() can be retrieved with Get().
TEST_F(SequenceLocalStorageSlotTest, GetSet) {
  SequenceLocalStorageSlot<int> slot;
  slot.Set(5);
  EXPECT_EQ(slot.Get(), 5);
}

// Verify that setting an object in a SequenceLocalStorageSlot creates a copy
// of that object independent of the original one.
TEST_F(SequenceLocalStorageSlotTest, SetObjectIsIndependent) {
  bool should_be_false = false;

  SequenceLocalStorageSlot<bool> slot;

  slot.Set(should_be_false);

  EXPECT_FALSE(slot.Get());
  slot.Get() = true;
  EXPECT_TRUE(slot.Get());

  EXPECT_NE(should_be_false, slot.Get());
}

// Verify that multiple slots work and that calling Get after overwriting
// a value in a slot yields the new value.
TEST_F(SequenceLocalStorageSlotTest, GetSetMultipleSlots) {
  SequenceLocalStorageSlot<int> slot1;
  SequenceLocalStorageSlot<int> slot2;
  SequenceLocalStorageSlot<int> slot3;

  slot1.Set(1);
  slot2.Set(2);
  slot3.Set(3);

  EXPECT_EQ(slot1.Get(), 1);
  EXPECT_EQ(slot2.Get(), 2);
  EXPECT_EQ(slot3.Get(), 3);

  slot3.Set(4);
  slot2.Set(5);
  slot1.Set(6);

  EXPECT_EQ(slot3.Get(), 4);
  EXPECT_EQ(slot2.Get(), 5);
  EXPECT_EQ(slot1.Get(), 6);
}

// Verify that changing the the value returned by Get() changes the value
// in sequence local storage.
TEST_F(SequenceLocalStorageSlotTest, GetReferenceModifiable) {
  SequenceLocalStorageSlot<bool> slot;
  slot.Set(false);
  slot.Get() = true;
  EXPECT_TRUE(slot.Get());
}

// Verify that a move-only type can be stored in sequence local storage.
TEST_F(SequenceLocalStorageSlotTest, SetGetWithMoveOnlyType) {
  std::unique_ptr<int> int_unique_ptr = std::make_unique<int>(5);

  SequenceLocalStorageSlot<std::unique_ptr<int>> slot;
  slot.Set(std::move(int_unique_ptr));

  EXPECT_EQ(*slot.Get(), 5);
}

// Verify that a Get() without a previous Set() on a slot returns a
// default-constructed value.
TEST_F(SequenceLocalStorageSlotTest, GetWithoutSetDefaultConstructs) {
  struct DefaultConstructable {
    int x = 0x12345678;
  };

  SequenceLocalStorageSlot<DefaultConstructable> slot;

  EXPECT_EQ(slot.Get().x, 0x12345678);
}

// Verify that a Get() without a previous Set() on a slot with a POD-type
// returns a default-constructed value.
// Note: this test could be flaky and give a false pass. If it's flaky, the test
// might've "passed" because the memory for the slot happened to be zeroed.
TEST_F(SequenceLocalStorageSlotTest, GetWithoutSetDefaultConstructsPOD) {
  SequenceLocalStorageSlot<void*> slot;

  EXPECT_EQ(slot.Get(), nullptr);
}

// Verify that the value of a slot is specific to a SequenceLocalStorageMap
TEST(SequenceLocalStorageSlotMultipleMapTest, SetGetMultipleMapsOneSlot) {
  SequenceLocalStorageSlot<unsigned int> slot;
  internal::SequenceLocalStorageMap sequence_local_storage_maps[5];

  // Set the value of the slot to be the index of the current
  // SequenceLocalStorageMaps in the vector
  for (unsigned int i = 0; i < arraysize(sequence_local_storage_maps); ++i) {
    internal::ScopedSetSequenceLocalStorageMapForCurrentThread
        scoped_sequence_local_storage(&sequence_local_storage_maps[i]);

    slot.Set(i);
  }

  for (unsigned int i = 0; i < arraysize(sequence_local_storage_maps); ++i) {
    internal::ScopedSetSequenceLocalStorageMapForCurrentThread
        scoped_sequence_local_storage(&sequence_local_storage_maps[i]);

    EXPECT_EQ(slot.Get(), i);
  }
}

}  // namespace base
