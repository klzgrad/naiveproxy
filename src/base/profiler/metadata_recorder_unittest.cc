// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/profiler/metadata_recorder.h"

#include "base/test/gtest_util.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {

bool operator==(const MetadataRecorder::Item& lhs,
                const MetadataRecorder::Item& rhs) {
  return lhs.name_hash == rhs.name_hash && lhs.value == rhs.value;
}

TEST(MetadataRecorderTest, GetItems_Empty) {
  MetadataRecorder recorder;
  MetadataRecorder::ItemArray items;
  size_t item_count = recorder.GetItems(&items);

  ASSERT_EQ(0u, item_count);
}

TEST(MetadataRecorderTest, Set_NewNameHash) {
  MetadataRecorder recorder;

  recorder.Set(10, 20);

  MetadataRecorder::ItemArray items;
  size_t item_count = recorder.GetItems(&items);
  ASSERT_EQ(1u, item_count);
  ASSERT_EQ(10u, items[0].name_hash);
  ASSERT_EQ(20, items[0].value);

  recorder.Set(20, 30);

  item_count = recorder.GetItems(&items);
  ASSERT_EQ(2u, item_count);
  ASSERT_EQ(20u, items[1].name_hash);
  ASSERT_EQ(30, items[1].value);
}

TEST(MetadataRecorderTest, Set_ExistingNameNash) {
  MetadataRecorder recorder;
  recorder.Set(10, 20);
  recorder.Set(10, 30);

  MetadataRecorder::ItemArray items;
  size_t item_count = recorder.GetItems(&items);
  ASSERT_EQ(1u, item_count);
  ASSERT_EQ(10u, items[0].name_hash);
  ASSERT_EQ(30, items[0].value);
}

TEST(MetadataRecorderTest, Set_ReAddRemovedNameNash) {
  MetadataRecorder recorder;
  MetadataRecorder::ItemArray items;
  std::vector<MetadataRecorder::Item> expected;
  for (size_t i = 0; i < items.size(); ++i) {
    expected.push_back(MetadataRecorder::Item{i, 0});
    recorder.Set(i, 0);
  }

  // By removing an item from a full recorder, re-setting the same item, and
  // verifying that the item is returned, we can verify that the recorder is
  // reusing the inactive slot for the same name hash instead of trying (and
  // failing) to allocate a new slot.
  recorder.Remove(3);
  recorder.Set(3, 0);

  size_t item_count = recorder.GetItems(&items);
  EXPECT_EQ(items.size(), item_count);
  ASSERT_THAT(expected, ::testing::ElementsAreArray(items));
}

TEST(MetadataRecorderTest, Set_AddPastMaxCount) {
  MetadataRecorder recorder;
  MetadataRecorder::ItemArray items;
  for (size_t i = 0; i < items.size(); ++i) {
    recorder.Set(i, 0);
  }

  ASSERT_DCHECK_DEATH(recorder.Set(items.size(), 0));
}

TEST(MetadataRecorderTest, Remove) {
  MetadataRecorder recorder;
  recorder.Set(10, 20);
  recorder.Set(30, 40);
  recorder.Set(50, 60);
  recorder.Remove(30);

  MetadataRecorder::ItemArray items;
  size_t item_count = recorder.GetItems(&items);
  ASSERT_EQ(2u, item_count);
  ASSERT_EQ(10u, items[0].name_hash);
  ASSERT_EQ(20, items[0].value);
  ASSERT_EQ(50u, items[1].name_hash);
  ASSERT_EQ(60, items[1].value);
}

TEST(MetadataRecorderTest, Remove_DoesntExist) {
  MetadataRecorder recorder;
  recorder.Set(10, 20);
  recorder.Remove(20);

  MetadataRecorder::ItemArray items;
  size_t item_count = recorder.GetItems(&items);
  ASSERT_EQ(1u, item_count);
  ASSERT_EQ(10u, items[0].name_hash);
  ASSERT_EQ(20, items[0].value);
}

}  // namespace base
