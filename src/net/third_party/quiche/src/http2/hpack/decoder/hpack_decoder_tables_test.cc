// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/hpack/decoder/hpack_decoder_tables.h"

#include <algorithm>
#include <string>
#include <tuple>
#include <vector>

#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "net/third_party/quiche/src/http2/hpack/http2_hpack_constants.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_logging.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_test_helpers.h"
#include "net/third_party/quiche/src/http2/test_tools/http2_random.h"
#include "net/third_party/quiche/src/http2/tools/random_util.h"

using ::testing::AssertionResult;
using ::testing::AssertionSuccess;

namespace http2 {
namespace test {
class HpackDecoderTablesPeer {
 public:
  static size_t num_dynamic_entries(const HpackDecoderTables& tables) {
    return tables.dynamic_table_.table_.size();
  }
};

namespace {
struct StaticEntry {
  const char* name;
  const char* value;
  size_t index;
};

std::vector<StaticEntry> MakeSpecStaticEntries() {
  std::vector<StaticEntry> static_entries;

#define STATIC_TABLE_ENTRY(name, value, index)                      \
  DCHECK_EQ(static_entries.size() + 1, static_cast<size_t>(index)); \
  static_entries.push_back({name, value, index});

#include "net/third_party/quiche/src/http2/hpack/hpack_static_table_entries.inc"

#undef STATIC_TABLE_ENTRY

  return static_entries;
}

template <class C>
void ShuffleCollection(C* collection, Http2Random* r) {
  std::shuffle(collection->begin(), collection->end(), *r);
}

class HpackDecoderStaticTableTest : public ::testing::Test {
 protected:
  HpackDecoderStaticTableTest() = default;

  std::vector<StaticEntry> shuffled_static_entries() {
    std::vector<StaticEntry> entries = MakeSpecStaticEntries();
    ShuffleCollection(&entries, &random_);
    return entries;
  }

  // This test is in a function so that it can be applied to both the static
  // table and the combined static+dynamic tables.
  AssertionResult VerifyStaticTableContents() {
    for (const auto& expected : shuffled_static_entries()) {
      const HpackStringPair* found = Lookup(expected.index);
      VERIFY_NE(found, nullptr);
      VERIFY_EQ(expected.name, found->name) << expected.index;
      VERIFY_EQ(expected.value, found->value) << expected.index;
    }

    // There should be no entry with index 0.
    VERIFY_EQ(nullptr, Lookup(0));
    return AssertionSuccess();
  }

  virtual const HpackStringPair* Lookup(size_t index) {
    return static_table_.Lookup(index);
  }

  Http2Random* RandomPtr() { return &random_; }

  Http2Random random_;

 private:
  HpackDecoderStaticTable static_table_;
};

TEST_F(HpackDecoderStaticTableTest, StaticTableContents) {
  EXPECT_TRUE(VerifyStaticTableContents());
}

size_t Size(const std::string& name, const std::string& value) {
  return name.size() + value.size() + 32;
}

// To support tests with more than a few of hand crafted changes to the dynamic
// table, we have another, exceedingly simple, implementation of the HPACK
// dynamic table containing FakeHpackEntry instances. We can thus compare the
// contents of the actual table with those in fake_dynamic_table_.

typedef std::tuple<std::string, std::string, size_t> FakeHpackEntry;
const std::string& Name(const FakeHpackEntry& entry) {
  return std::get<0>(entry);
}
const std::string& Value(const FakeHpackEntry& entry) {
  return std::get<1>(entry);
}
size_t Size(const FakeHpackEntry& entry) {
  return std::get<2>(entry);
}

class HpackDecoderTablesTest : public HpackDecoderStaticTableTest {
 protected:
  const HpackStringPair* Lookup(size_t index) override {
    return tables_.Lookup(index);
  }

  size_t dynamic_size_limit() const {
    return tables_.header_table_size_limit();
  }
  size_t current_dynamic_size() const {
    return tables_.current_header_table_size();
  }
  size_t num_dynamic_entries() const {
    return HpackDecoderTablesPeer::num_dynamic_entries(tables_);
  }

  // Insert the name and value into fake_dynamic_table_.
  void FakeInsert(const std::string& name, const std::string& value) {
    FakeHpackEntry entry(name, value, Size(name, value));
    fake_dynamic_table_.insert(fake_dynamic_table_.begin(), entry);
  }

  // Add up the size of all entries in fake_dynamic_table_.
  size_t FakeSize() {
    size_t sz = 0;
    for (const auto& entry : fake_dynamic_table_) {
      sz += Size(entry);
    }
    return sz;
  }

  // If the total size of the fake_dynamic_table_ is greater than limit,
  // keep the first N entries such that those N entries have a size not
  // greater than limit, and such that keeping entry N+1 would have a size
  // greater than limit. Returns the count of removed bytes.
  size_t FakeTrim(size_t limit) {
    size_t original_size = FakeSize();
    size_t total_size = 0;
    for (size_t ndx = 0; ndx < fake_dynamic_table_.size(); ++ndx) {
      total_size += Size(fake_dynamic_table_[ndx]);
      if (total_size > limit) {
        // Need to get rid of ndx and all following entries.
        fake_dynamic_table_.erase(fake_dynamic_table_.begin() + ndx,
                                  fake_dynamic_table_.end());
        return original_size - FakeSize();
      }
    }
    return 0;
  }

  // Verify that the contents of the actual dynamic table match those in
  // fake_dynamic_table_.
  AssertionResult VerifyDynamicTableContents() {
    VERIFY_EQ(current_dynamic_size(), FakeSize());
    VERIFY_EQ(num_dynamic_entries(), fake_dynamic_table_.size());

    for (size_t ndx = 0; ndx < fake_dynamic_table_.size(); ++ndx) {
      const HpackStringPair* found = Lookup(ndx + kFirstDynamicTableIndex);
      VERIFY_NE(found, nullptr);

      const auto& expected = fake_dynamic_table_[ndx];
      VERIFY_EQ(Name(expected), found->name);
      VERIFY_EQ(Value(expected), found->value);
    }

    // Make sure there are no more entries.
    VERIFY_EQ(nullptr,
              Lookup(fake_dynamic_table_.size() + kFirstDynamicTableIndex));
    return AssertionSuccess();
  }

  // Apply an update to the limit on the maximum size of the dynamic table.
  AssertionResult DynamicTableSizeUpdate(size_t size_limit) {
    VERIFY_EQ(current_dynamic_size(), FakeSize());
    if (size_limit < current_dynamic_size()) {
      // Will need to trim the dynamic table's oldest entries.
      tables_.DynamicTableSizeUpdate(size_limit);
      FakeTrim(size_limit);
      return VerifyDynamicTableContents();
    }
    // Shouldn't change the size.
    tables_.DynamicTableSizeUpdate(size_limit);
    return VerifyDynamicTableContents();
  }

  // Insert an entry into the dynamic table, confirming that trimming of entries
  // occurs if the total size is greater than the limit, and that older entries
  // move up by 1 index.
  AssertionResult Insert(const std::string& name, const std::string& value) {
    size_t old_count = num_dynamic_entries();
    tables_.Insert(HpackString(name), HpackString(value));
    FakeInsert(name, value);
    VERIFY_EQ(old_count + 1, fake_dynamic_table_.size());
    FakeTrim(dynamic_size_limit());
    VERIFY_EQ(current_dynamic_size(), FakeSize());
    VERIFY_EQ(num_dynamic_entries(), fake_dynamic_table_.size());
    return VerifyDynamicTableContents();
  }

 private:
  HpackDecoderTables tables_;

  std::vector<FakeHpackEntry> fake_dynamic_table_;
};

TEST_F(HpackDecoderTablesTest, StaticTableContents) {
  EXPECT_TRUE(VerifyStaticTableContents());
}

// Generate a bunch of random header entries, insert them, and confirm they
// present, as required by the RFC, using VerifyDynamicTableContents above on
// each Insert. Also apply various resizings of the dynamic table.
TEST_F(HpackDecoderTablesTest, RandomDynamicTable) {
  EXPECT_EQ(0u, current_dynamic_size());
  EXPECT_TRUE(VerifyStaticTableContents());
  EXPECT_TRUE(VerifyDynamicTableContents());

  std::vector<size_t> table_sizes;
  table_sizes.push_back(dynamic_size_limit());
  table_sizes.push_back(0);
  table_sizes.push_back(dynamic_size_limit() / 2);
  table_sizes.push_back(dynamic_size_limit());
  table_sizes.push_back(dynamic_size_limit() / 2);
  table_sizes.push_back(0);
  table_sizes.push_back(dynamic_size_limit());

  for (size_t limit : table_sizes) {
    ASSERT_TRUE(DynamicTableSizeUpdate(limit));
    for (int insert_count = 0; insert_count < 100; ++insert_count) {
      std::string name =
          GenerateHttp2HeaderName(random_.UniformInRange(2, 40), RandomPtr());
      std::string value =
          GenerateWebSafeString(random_.UniformInRange(2, 600), RandomPtr());
      ASSERT_TRUE(Insert(name, value));
    }
    EXPECT_TRUE(VerifyStaticTableContents());
  }
}

}  // namespace
}  // namespace test
}  // namespace http2
