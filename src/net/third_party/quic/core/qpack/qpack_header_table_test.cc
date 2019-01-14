#include "net/third_party/quic/core/qpack/qpack_header_table.h"

#include "net/third_party/quic/platform/api/quic_test.h"
#include "net/third_party/spdy/core/hpack/hpack_entry.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace quic {
namespace test {
namespace {

class QpackHeaderTableTest : public QuicTest {
 protected:
  QpackHeaderTable table_;
};

TEST_F(QpackHeaderTableTest, LookupEntry) {
  const auto* entry = table_.LookupEntry(0);
  EXPECT_FALSE(entry);

  entry = table_.LookupEntry(1);
  EXPECT_EQ(":authority", entry->name());
  EXPECT_EQ("", entry->value());

  entry = table_.LookupEntry(2);
  EXPECT_EQ(":method", entry->name());
  EXPECT_EQ("GET", entry->value());

  entry = table_.LookupEntry(61);
  EXPECT_EQ("www-authenticate", entry->name());
  EXPECT_EQ("", entry->value());

  entry = table_.LookupEntry(62);
  EXPECT_FALSE(entry);
}

TEST_F(QpackHeaderTableTest, FindHeaderField) {
  // A header name that has multiple entries with different values.
  size_t index = 0;
  QpackHeaderTable::MatchType matchtype =
      table_.FindHeaderField(":method", "GET", &index);
  EXPECT_EQ(QpackHeaderTable::MatchType::kNameAndValue, matchtype);
  EXPECT_EQ(2u, index);

  matchtype = table_.FindHeaderField(":method", "POST", &index);
  EXPECT_EQ(QpackHeaderTable::MatchType::kNameAndValue, matchtype);
  EXPECT_EQ(3u, index);

  matchtype = table_.FindHeaderField(":method", "CONNECT", &index);
  EXPECT_EQ(QpackHeaderTable::MatchType::kName, matchtype);
  EXPECT_EQ(2u, index);

  // A header name that has a single entry with non-empty value.
  matchtype =
      table_.FindHeaderField("accept-encoding", "gzip, deflate", &index);
  EXPECT_EQ(QpackHeaderTable::MatchType::kNameAndValue, matchtype);
  EXPECT_EQ(16u, index);

  matchtype = table_.FindHeaderField("accept-encoding", "brotli", &index);
  EXPECT_EQ(QpackHeaderTable::MatchType::kName, matchtype);
  EXPECT_EQ(16u, index);

  matchtype = table_.FindHeaderField("accept-encoding", "", &index);
  EXPECT_EQ(QpackHeaderTable::MatchType::kName, matchtype);
  EXPECT_EQ(16u, index);

  // A header name that has a single entry with empty value.
  matchtype = table_.FindHeaderField("cache-control", "", &index);
  EXPECT_EQ(QpackHeaderTable::MatchType::kNameAndValue, matchtype);
  EXPECT_EQ(24u, index);

  matchtype = table_.FindHeaderField("cache-control", "foo", &index);
  EXPECT_EQ(QpackHeaderTable::MatchType::kName, matchtype);
  EXPECT_EQ(24u, index);

  // No matching header name.
  matchtype = table_.FindHeaderField("foo", "", &index);
  EXPECT_EQ(QpackHeaderTable::MatchType::kNoMatch, matchtype);

  matchtype = table_.FindHeaderField("foo", "bar", &index);
  EXPECT_EQ(QpackHeaderTable::MatchType::kNoMatch, matchtype);
}

}  // namespace
}  // namespace test
}  // namespace quic
