// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/qpack/qpack_header_table.h"

#include "net/third_party/quiche/src/quic/core/qpack/qpack_static_table.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_arraysize.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/spdy/core/hpack/hpack_entry.h"

using ::testing::Mock;
using ::testing::StrictMock;

namespace quic {
namespace test {
namespace {

const uint64_t kMaximumDynamicTableCapacityForTesting = 1024 * 1024;

class MockObserver : public QpackHeaderTable::Observer {
 public:
  ~MockObserver() override = default;

  MOCK_METHOD0(OnInsertCountReachedThreshold, void());
};

class QpackHeaderTableTest : public QuicTest {
 protected:
  QpackHeaderTableTest() {
    table_.SetMaximumDynamicTableCapacity(
        kMaximumDynamicTableCapacityForTesting);
    table_.SetDynamicTableCapacity(kMaximumDynamicTableCapacityForTesting);
  }
  ~QpackHeaderTableTest() override = default;

  void ExpectEntryAtIndex(bool is_static,
                          uint64_t index,
                          QuicStringPiece expected_name,
                          QuicStringPiece expected_value) const {
    const auto* entry = table_.LookupEntry(is_static, index);
    ASSERT_TRUE(entry);
    EXPECT_EQ(expected_name, entry->name());
    EXPECT_EQ(expected_value, entry->value());
  }

  void ExpectNoEntryAtIndex(bool is_static, uint64_t index) const {
    EXPECT_FALSE(table_.LookupEntry(is_static, index));
  }

  void ExpectMatch(QuicStringPiece name,
                   QuicStringPiece value,
                   QpackHeaderTable::MatchType expected_match_type,
                   bool expected_is_static,
                   uint64_t expected_index) const {
    // Initialize outparams to a value different from the expected to ensure
    // that FindHeaderField() sets them.
    bool is_static = !expected_is_static;
    uint64_t index = expected_index + 1;

    QpackHeaderTable::MatchType matchtype =
        table_.FindHeaderField(name, value, &is_static, &index);

    EXPECT_EQ(expected_match_type, matchtype) << name << ": " << value;
    EXPECT_EQ(expected_is_static, is_static) << name << ": " << value;
    EXPECT_EQ(expected_index, index) << name << ": " << value;
  }

  void ExpectNoMatch(QuicStringPiece name, QuicStringPiece value) const {
    bool is_static = false;
    uint64_t index = 0;

    QpackHeaderTable::MatchType matchtype =
        table_.FindHeaderField(name, value, &is_static, &index);

    EXPECT_EQ(QpackHeaderTable::MatchType::kNoMatch, matchtype)
        << name << ": " << value;
  }

  void InsertEntry(QuicStringPiece name, QuicStringPiece value) {
    EXPECT_TRUE(table_.InsertEntry(name, value));
  }

  void ExpectToFailInsertingEntry(QuicStringPiece name, QuicStringPiece value) {
    EXPECT_FALSE(table_.InsertEntry(name, value));
  }

  bool SetDynamicTableCapacity(uint64_t capacity) {
    return table_.SetDynamicTableCapacity(capacity);
  }

  void RegisterObserver(QpackHeaderTable::Observer* observer,
                        uint64_t required_insert_count) {
    table_.RegisterObserver(observer, required_insert_count);
  }

  uint64_t max_entries() const { return table_.max_entries(); }
  uint64_t inserted_entry_count() const {
    return table_.inserted_entry_count();
  }
  uint64_t dropped_entry_count() const { return table_.dropped_entry_count(); }

 private:
  QpackHeaderTable table_;
};

TEST_F(QpackHeaderTableTest, LookupStaticEntry) {
  ExpectEntryAtIndex(/* is_static = */ true, 0, ":authority", "");

  ExpectEntryAtIndex(/* is_static = */ true, 1, ":path", "/");

  // 98 is the last entry.
  ExpectEntryAtIndex(/* is_static = */ true, 98, "x-frame-options",
                     "sameorigin");

  ASSERT_EQ(99u, QpackStaticTableVector().size());
  ExpectNoEntryAtIndex(/* is_static = */ true, 99);
}

TEST_F(QpackHeaderTableTest, InsertAndLookupDynamicEntry) {
  // Dynamic table is initially entry.
  ExpectNoEntryAtIndex(/* is_static = */ false, 0);
  ExpectNoEntryAtIndex(/* is_static = */ false, 1);
  ExpectNoEntryAtIndex(/* is_static = */ false, 2);
  ExpectNoEntryAtIndex(/* is_static = */ false, 3);

  // Insert one entry.
  InsertEntry("foo", "bar");

  ExpectEntryAtIndex(/* is_static = */ false, 0, "foo", "bar");

  ExpectNoEntryAtIndex(/* is_static = */ false, 1);
  ExpectNoEntryAtIndex(/* is_static = */ false, 2);
  ExpectNoEntryAtIndex(/* is_static = */ false, 3);

  // Insert a different entry.
  InsertEntry("baz", "bing");

  ExpectEntryAtIndex(/* is_static = */ false, 0, "foo", "bar");

  ExpectEntryAtIndex(/* is_static = */ false, 1, "baz", "bing");

  ExpectNoEntryAtIndex(/* is_static = */ false, 2);
  ExpectNoEntryAtIndex(/* is_static = */ false, 3);

  // Insert an entry identical to the most recently inserted one.
  InsertEntry("baz", "bing");

  ExpectEntryAtIndex(/* is_static = */ false, 0, "foo", "bar");

  ExpectEntryAtIndex(/* is_static = */ false, 1, "baz", "bing");

  ExpectEntryAtIndex(/* is_static = */ false, 2, "baz", "bing");

  ExpectNoEntryAtIndex(/* is_static = */ false, 3);
}

TEST_F(QpackHeaderTableTest, FindStaticHeaderField) {
  // A header name that has multiple entries with different values.
  ExpectMatch(":method", "GET", QpackHeaderTable::MatchType::kNameAndValue,
              true, 17u);

  ExpectMatch(":method", "POST", QpackHeaderTable::MatchType::kNameAndValue,
              true, 20u);

  ExpectMatch(":method", "TRACE", QpackHeaderTable::MatchType::kName, true,
              15u);

  // A header name that has a single entry with non-empty value.
  ExpectMatch("accept-encoding", "gzip, deflate, br",
              QpackHeaderTable::MatchType::kNameAndValue, true, 31u);

  ExpectMatch("accept-encoding", "compress", QpackHeaderTable::MatchType::kName,
              true, 31u);

  ExpectMatch("accept-encoding", "", QpackHeaderTable::MatchType::kName, true,
              31u);

  // A header name that has a single entry with empty value.
  ExpectMatch("location", "", QpackHeaderTable::MatchType::kNameAndValue, true,
              12u);

  ExpectMatch("location", "foo", QpackHeaderTable::MatchType::kName, true, 12u);

  // No matching header name.
  ExpectNoMatch("foo", "");
  ExpectNoMatch("foo", "bar");
}

TEST_F(QpackHeaderTableTest, FindDynamicHeaderField) {
  // Dynamic table is initially entry.
  ExpectNoMatch("foo", "bar");
  ExpectNoMatch("foo", "baz");

  // Insert one entry.
  InsertEntry("foo", "bar");

  // Match name and value.
  ExpectMatch("foo", "bar", QpackHeaderTable::MatchType::kNameAndValue, false,
              0u);

  // Match name only.
  ExpectMatch("foo", "baz", QpackHeaderTable::MatchType::kName, false, 0u);

  // Insert an identical entry.  FindHeaderField() should return the index of
  // the most recently inserted matching entry.
  InsertEntry("foo", "bar");

  // Match name and value.
  ExpectMatch("foo", "bar", QpackHeaderTable::MatchType::kNameAndValue, false,
              1u);

  // Match name only.
  ExpectMatch("foo", "baz", QpackHeaderTable::MatchType::kName, false, 1u);
}

TEST_F(QpackHeaderTableTest, FindHeaderFieldPrefersStaticTable) {
  // Insert an entry to the dynamic table that exists in the static table.
  InsertEntry(":method", "GET");

  // Insertion works.
  ExpectEntryAtIndex(/* is_static = */ false, 0, ":method", "GET");

  // FindHeaderField() prefers static table if both have name-and-value match.
  ExpectMatch(":method", "GET", QpackHeaderTable::MatchType::kNameAndValue,
              true, 17u);

  // FindHeaderField() prefers static table if both have name match but no value
  // match, and prefers the first entry with matching name.
  ExpectMatch(":method", "TRACE", QpackHeaderTable::MatchType::kName, true,
              15u);

  // Add new entry to the dynamic table.
  InsertEntry(":method", "TRACE");

  // FindHeaderField prefers name-and-value match in dynamic table over name
  // only match in static table.
  ExpectMatch(":method", "TRACE", QpackHeaderTable::MatchType::kNameAndValue,
              false, 1u);
}

// MaxEntries is determined by maximum dynamic table capacity,
// which is set at construction time.
TEST_F(QpackHeaderTableTest, MaxEntries) {
  QpackHeaderTable table1;
  table1.SetMaximumDynamicTableCapacity(1024);
  EXPECT_EQ(32u, table1.max_entries());

  QpackHeaderTable table2;
  table2.SetMaximumDynamicTableCapacity(500);
  EXPECT_EQ(15u, table2.max_entries());
}

TEST_F(QpackHeaderTableTest, SetDynamicTableCapacity) {
  // Dynamic table capacity does not affect MaxEntries.
  EXPECT_TRUE(SetDynamicTableCapacity(1024));
  EXPECT_EQ(32u * 1024, max_entries());

  EXPECT_TRUE(SetDynamicTableCapacity(500));
  EXPECT_EQ(32u * 1024, max_entries());

  // Dynamic table capacity cannot exceed maximum dynamic table capacity.
  EXPECT_FALSE(
      SetDynamicTableCapacity(2 * kMaximumDynamicTableCapacityForTesting));
}

TEST_F(QpackHeaderTableTest, EvictByInsertion) {
  EXPECT_TRUE(SetDynamicTableCapacity(40));

  // Entry size is 3 + 3 + 32 = 38.
  InsertEntry("foo", "bar");
  EXPECT_EQ(1u, inserted_entry_count());
  EXPECT_EQ(0u, dropped_entry_count());

  ExpectMatch("foo", "bar", QpackHeaderTable::MatchType::kNameAndValue,
              /* expected_is_static = */ false, 0u);

  // Inserting second entry evicts the first one.
  InsertEntry("baz", "qux");
  EXPECT_EQ(2u, inserted_entry_count());
  EXPECT_EQ(1u, dropped_entry_count());

  ExpectNoMatch("foo", "bar");
  ExpectMatch("baz", "qux", QpackHeaderTable::MatchType::kNameAndValue,
              /* expected_is_static = */ false, 1u);

  // Inserting an entry that does not fit results in error.
  ExpectToFailInsertingEntry("foobar", "foobar");
}

TEST_F(QpackHeaderTableTest, EvictByUpdateTableSize) {
  // Entry size is 3 + 3 + 32 = 38.
  InsertEntry("foo", "bar");
  InsertEntry("baz", "qux");
  EXPECT_EQ(2u, inserted_entry_count());
  EXPECT_EQ(0u, dropped_entry_count());

  ExpectMatch("foo", "bar", QpackHeaderTable::MatchType::kNameAndValue,
              /* expected_is_static = */ false, 0u);
  ExpectMatch("baz", "qux", QpackHeaderTable::MatchType::kNameAndValue,
              /* expected_is_static = */ false, 1u);

  EXPECT_TRUE(SetDynamicTableCapacity(40));
  EXPECT_EQ(2u, inserted_entry_count());
  EXPECT_EQ(1u, dropped_entry_count());

  ExpectNoMatch("foo", "bar");
  ExpectMatch("baz", "qux", QpackHeaderTable::MatchType::kNameAndValue,
              /* expected_is_static = */ false, 1u);

  EXPECT_TRUE(SetDynamicTableCapacity(20));
  EXPECT_EQ(2u, inserted_entry_count());
  EXPECT_EQ(2u, dropped_entry_count());

  ExpectNoMatch("foo", "bar");
  ExpectNoMatch("baz", "qux");
}

TEST_F(QpackHeaderTableTest, EvictOldestOfIdentical) {
  EXPECT_TRUE(SetDynamicTableCapacity(80));

  // Entry size is 3 + 3 + 32 = 38.
  // Insert same entry twice.
  InsertEntry("foo", "bar");
  InsertEntry("foo", "bar");
  EXPECT_EQ(2u, inserted_entry_count());
  EXPECT_EQ(0u, dropped_entry_count());

  // Find most recently inserted entry.
  ExpectMatch("foo", "bar", QpackHeaderTable::MatchType::kNameAndValue,
              /* expected_is_static = */ false, 1u);

  // Inserting third entry evicts the first one, not the second.
  InsertEntry("baz", "qux");
  EXPECT_EQ(3u, inserted_entry_count());
  EXPECT_EQ(1u, dropped_entry_count());

  ExpectMatch("foo", "bar", QpackHeaderTable::MatchType::kNameAndValue,
              /* expected_is_static = */ false, 1u);
  ExpectMatch("baz", "qux", QpackHeaderTable::MatchType::kNameAndValue,
              /* expected_is_static = */ false, 2u);
}

TEST_F(QpackHeaderTableTest, EvictOldestOfSameName) {
  EXPECT_TRUE(SetDynamicTableCapacity(80));

  // Entry size is 3 + 3 + 32 = 38.
  // Insert two entries with same name but different values.
  InsertEntry("foo", "bar");
  InsertEntry("foo", "baz");
  EXPECT_EQ(2u, inserted_entry_count());
  EXPECT_EQ(0u, dropped_entry_count());

  // Find most recently inserted entry with matching name.
  ExpectMatch("foo", "foo", QpackHeaderTable::MatchType::kName,
              /* expected_is_static = */ false, 1u);

  // Inserting third entry evicts the first one, not the second.
  InsertEntry("baz", "qux");
  EXPECT_EQ(3u, inserted_entry_count());
  EXPECT_EQ(1u, dropped_entry_count());

  ExpectMatch("foo", "foo", QpackHeaderTable::MatchType::kName,
              /* expected_is_static = */ false, 1u);
  ExpectMatch("baz", "qux", QpackHeaderTable::MatchType::kNameAndValue,
              /* expected_is_static = */ false, 2u);
}

// Returns the size of the largest entry that could be inserted into the
// dynamic table without evicting entry |index|.
TEST_F(QpackHeaderTableTest, MaxInsertSizeWithoutEvictingGivenEntry) {
  const uint64_t dynamic_table_capacity = 100;
  QpackHeaderTable table;
  table.SetMaximumDynamicTableCapacity(dynamic_table_capacity);
  EXPECT_TRUE(table.SetDynamicTableCapacity(dynamic_table_capacity));

  // Empty table can take an entry up to its capacity.
  EXPECT_EQ(dynamic_table_capacity,
            table.MaxInsertSizeWithoutEvictingGivenEntry(0));

  const uint64_t entry_size1 = QpackEntry::Size("foo", "bar");
  EXPECT_TRUE(table.InsertEntry("foo", "bar"));
  EXPECT_EQ(dynamic_table_capacity - entry_size1,
            table.MaxInsertSizeWithoutEvictingGivenEntry(0));
  // Table can take an entry up to its capacity if all entries are allowed to be
  // evicted.
  EXPECT_EQ(dynamic_table_capacity,
            table.MaxInsertSizeWithoutEvictingGivenEntry(1));

  const uint64_t entry_size2 = QpackEntry::Size("baz", "foobar");
  EXPECT_TRUE(table.InsertEntry("baz", "foobar"));
  // Table can take an entry up to its capacity if all entries are allowed to be
  // evicted.
  EXPECT_EQ(dynamic_table_capacity,
            table.MaxInsertSizeWithoutEvictingGivenEntry(2));
  // Second entry must stay.
  EXPECT_EQ(dynamic_table_capacity - entry_size2,
            table.MaxInsertSizeWithoutEvictingGivenEntry(1));
  // First and second entry must stay.
  EXPECT_EQ(dynamic_table_capacity - entry_size2 - entry_size1,
            table.MaxInsertSizeWithoutEvictingGivenEntry(0));

  // Third entry evicts first one.
  const uint64_t entry_size3 = QpackEntry::Size("last", "entry");
  EXPECT_TRUE(table.InsertEntry("last", "entry"));
  EXPECT_EQ(1u, table.dropped_entry_count());
  // Table can take an entry up to its capacity if all entries are allowed to be
  // evicted.
  EXPECT_EQ(dynamic_table_capacity,
            table.MaxInsertSizeWithoutEvictingGivenEntry(3));
  // Third entry must stay.
  EXPECT_EQ(dynamic_table_capacity - entry_size3,
            table.MaxInsertSizeWithoutEvictingGivenEntry(2));
  // Second and third entry must stay.
  EXPECT_EQ(dynamic_table_capacity - entry_size3 - entry_size2,
            table.MaxInsertSizeWithoutEvictingGivenEntry(1));
}

TEST_F(QpackHeaderTableTest, Observer) {
  StrictMock<MockObserver> observer1;
  RegisterObserver(&observer1, 1);
  EXPECT_CALL(observer1, OnInsertCountReachedThreshold);
  InsertEntry("foo", "bar");
  EXPECT_EQ(1u, inserted_entry_count());
  Mock::VerifyAndClearExpectations(&observer1);

  // Registration order does not matter.
  StrictMock<MockObserver> observer2;
  StrictMock<MockObserver> observer3;
  RegisterObserver(&observer3, 3);
  RegisterObserver(&observer2, 2);

  EXPECT_CALL(observer2, OnInsertCountReachedThreshold);
  InsertEntry("foo", "bar");
  EXPECT_EQ(2u, inserted_entry_count());
  Mock::VerifyAndClearExpectations(&observer3);

  EXPECT_CALL(observer3, OnInsertCountReachedThreshold);
  InsertEntry("foo", "bar");
  EXPECT_EQ(3u, inserted_entry_count());
  Mock::VerifyAndClearExpectations(&observer2);

  // Multiple observers with identical |required_insert_count| should all be
  // notified.
  StrictMock<MockObserver> observer4;
  StrictMock<MockObserver> observer5;
  RegisterObserver(&observer4, 4);
  RegisterObserver(&observer5, 4);

  EXPECT_CALL(observer4, OnInsertCountReachedThreshold);
  EXPECT_CALL(observer5, OnInsertCountReachedThreshold);
  InsertEntry("foo", "bar");
  EXPECT_EQ(4u, inserted_entry_count());
  Mock::VerifyAndClearExpectations(&observer4);
  Mock::VerifyAndClearExpectations(&observer5);
}

TEST_F(QpackHeaderTableTest, DrainingIndex) {
  QpackHeaderTable table;
  table.SetMaximumDynamicTableCapacity(kMaximumDynamicTableCapacityForTesting);
  EXPECT_TRUE(
      table.SetDynamicTableCapacity(4 * QpackEntry::Size("foo", "bar")));

  // Empty table: no draining entry.
  EXPECT_EQ(0u, table.draining_index(0.0));
  EXPECT_EQ(0u, table.draining_index(1.0));

  // Table with one entry.
  EXPECT_TRUE(table.InsertEntry("foo", "bar"));
  // Any entry can be referenced if none of the table is draining.
  EXPECT_EQ(0u, table.draining_index(0.0));
  // No entry can be referenced if all of the table is draining.
  EXPECT_EQ(1u, table.draining_index(1.0));

  // Table with two entries is at half capacity.
  EXPECT_TRUE(table.InsertEntry("foo", "bar"));
  // Any entry can be referenced if at most half of the table is draining,
  // because current entries only take up half of total capacity.
  EXPECT_EQ(0u, table.draining_index(0.0));
  EXPECT_EQ(0u, table.draining_index(0.5));
  // No entry can be referenced if all of the table is draining.
  EXPECT_EQ(2u, table.draining_index(1.0));

  // Table with four entries is full.
  EXPECT_TRUE(table.InsertEntry("foo", "bar"));
  EXPECT_TRUE(table.InsertEntry("foo", "bar"));
  // Any entry can be referenced if none of the table is draining.
  EXPECT_EQ(0u, table.draining_index(0.0));
  // In a full table with identically sized entries, |draining_fraction| of all
  // entries are draining.
  EXPECT_EQ(2u, table.draining_index(0.5));
  // No entry can be referenced if all of the table is draining.
  EXPECT_EQ(4u, table.draining_index(1.0));
}

}  // namespace
}  // namespace test
}  // namespace quic
