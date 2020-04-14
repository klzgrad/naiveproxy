// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/spdy/core/hpack/hpack_entry.h"

#include "net/third_party/quiche/src/spdy/platform/api/spdy_test.h"

namespace spdy {

namespace {

class HpackEntryTest : public ::testing::Test {
 protected:
  HpackEntryTest()
      : name_("header-name"),
        value_("header value"),
        total_insertions_(0),
        table_size_(0) {}

  // These builders maintain the same external table invariants that a "real"
  // table (ie HpackHeaderTable) would.
  HpackEntry StaticEntry() {
    return HpackEntry(name_, value_, true, total_insertions_++);
  }
  HpackEntry DynamicEntry() {
    ++table_size_;
    size_t index = total_insertions_++;
    return HpackEntry(name_, value_, false, index);
  }
  void DropEntry() { --table_size_; }

  size_t IndexOf(const HpackEntry& entry) const {
    if (entry.IsStatic()) {
      return 1 + entry.InsertionIndex() + table_size_;
    } else {
      return total_insertions_ - entry.InsertionIndex();
    }
  }

  size_t Size() {
    return name_.size() + value_.size() + HpackEntry::kSizeOverhead;
  }

  std::string name_, value_;

 private:
  // Referenced by HpackEntry instances.
  size_t total_insertions_;
  size_t table_size_;
};

TEST_F(HpackEntryTest, StaticConstructor) {
  HpackEntry entry(StaticEntry());

  EXPECT_EQ(name_, entry.name());
  EXPECT_EQ(value_, entry.value());
  EXPECT_TRUE(entry.IsStatic());
  EXPECT_EQ(1u, IndexOf(entry));
  EXPECT_EQ(Size(), entry.Size());
}

TEST_F(HpackEntryTest, DynamicConstructor) {
  HpackEntry entry(DynamicEntry());

  EXPECT_EQ(name_, entry.name());
  EXPECT_EQ(value_, entry.value());
  EXPECT_FALSE(entry.IsStatic());
  EXPECT_EQ(1u, IndexOf(entry));
  EXPECT_EQ(Size(), entry.Size());
}

TEST_F(HpackEntryTest, LookupConstructor) {
  HpackEntry entry(name_, value_);

  EXPECT_EQ(name_, entry.name());
  EXPECT_EQ(value_, entry.value());
  EXPECT_FALSE(entry.IsStatic());
  EXPECT_EQ(0u, IndexOf(entry));
  EXPECT_EQ(Size(), entry.Size());
}

TEST_F(HpackEntryTest, DefaultConstructor) {
  HpackEntry entry;

  EXPECT_TRUE(entry.name().empty());
  EXPECT_TRUE(entry.value().empty());
  EXPECT_EQ(HpackEntry::kSizeOverhead, entry.Size());
}

TEST_F(HpackEntryTest, IndexUpdate) {
  HpackEntry static1(StaticEntry());
  HpackEntry static2(StaticEntry());

  EXPECT_EQ(1u, IndexOf(static1));
  EXPECT_EQ(2u, IndexOf(static2));

  HpackEntry dynamic1(DynamicEntry());
  HpackEntry dynamic2(DynamicEntry());

  EXPECT_EQ(1u, IndexOf(dynamic2));
  EXPECT_EQ(2u, IndexOf(dynamic1));
  EXPECT_EQ(3u, IndexOf(static1));
  EXPECT_EQ(4u, IndexOf(static2));

  DropEntry();  // Drops |dynamic1|.

  EXPECT_EQ(1u, IndexOf(dynamic2));
  EXPECT_EQ(2u, IndexOf(static1));
  EXPECT_EQ(3u, IndexOf(static2));

  HpackEntry dynamic3(DynamicEntry());

  EXPECT_EQ(1u, IndexOf(dynamic3));
  EXPECT_EQ(2u, IndexOf(dynamic2));
  EXPECT_EQ(3u, IndexOf(static1));
  EXPECT_EQ(4u, IndexOf(static2));
}

}  // namespace

}  // namespace spdy
