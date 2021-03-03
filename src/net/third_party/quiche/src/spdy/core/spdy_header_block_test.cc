// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "spdy/core/spdy_header_block.h"

#include <memory>
#include <utility>

#include "common/platform/api/quiche_test.h"
#include "spdy/core/spdy_test_utils.h"

using ::testing::ElementsAre;

namespace spdy {
namespace test {

class ValueProxyPeer {
 public:
  static absl::string_view key(Http2HeaderBlock::ValueProxy* p) {
    return p->key_;
  }
};

std::pair<absl::string_view, absl::string_view> Pair(absl::string_view k,
                                                     absl::string_view v) {
  return std::make_pair(k, v);
}

// This test verifies that Http2HeaderBlock behaves correctly when empty.
TEST(Http2HeaderBlockTest, EmptyBlock) {
  Http2HeaderBlock block;
  EXPECT_TRUE(block.empty());
  EXPECT_EQ(0u, block.size());
  EXPECT_EQ(block.end(), block.find("foo"));
  EXPECT_TRUE(block.end() == block.begin());

  // Should have no effect.
  block.erase("bar");
}

TEST(Http2HeaderBlockTest, KeyMemoryReclaimedOnLookup) {
  Http2HeaderBlock block;
  absl::string_view copied_key1;
  {
    auto proxy1 = block["some key name"];
    copied_key1 = ValueProxyPeer::key(&proxy1);
  }
  absl::string_view copied_key2;
  {
    auto proxy2 = block["some other key name"];
    copied_key2 = ValueProxyPeer::key(&proxy2);
  }
  // Because proxy1 was never used to modify the block, the memory used for the
  // key could be reclaimed and used for the second call to operator[].
  // Therefore, we expect the pointers of the two absl::string_views to be
  // equal.
  EXPECT_EQ(copied_key1.data(), copied_key2.data());

  {
    auto proxy1 = block["some key name"];
    block["some other key name"] = "some value";
  }
  // Nothing should blow up when proxy1 is destructed, and we should be able to
  // modify and access the Http2HeaderBlock.
  block["key"] = "value";
  EXPECT_EQ("value", block["key"]);
  EXPECT_EQ("some value", block["some other key name"]);
  EXPECT_TRUE(block.find("some key name") == block.end());
}

// This test verifies that headers can be set in a variety of ways.
TEST(Http2HeaderBlockTest, AddHeaders) {
  Http2HeaderBlock block;
  block["foo"] = std::string(300, 'x');
  block["bar"] = "baz";
  block["qux"] = "qux1";
  block["qux"] = "qux2";
  block.insert(std::make_pair("key", "value"));

  EXPECT_EQ(Pair("foo", std::string(300, 'x')), *block.find("foo"));
  EXPECT_EQ("baz", block["bar"]);
  std::string qux("qux");
  EXPECT_EQ("qux2", block[qux]);
  ASSERT_NE(block.end(), block.find("key"));
  EXPECT_EQ(Pair("key", "value"), *block.find("key"));

  block.erase("key");
  EXPECT_EQ(block.end(), block.find("key"));
}

// This test verifies that Http2HeaderBlock can be copied using Clone().
TEST(Http2HeaderBlockTest, CopyBlocks) {
  Http2HeaderBlock block1;
  block1["foo"] = std::string(300, 'x');
  block1["bar"] = "baz";
  block1.insert(std::make_pair("qux", "qux1"));

  Http2HeaderBlock block2 = block1.Clone();
  Http2HeaderBlock block3(block1.Clone());

  EXPECT_EQ(block1, block2);
  EXPECT_EQ(block1, block3);
}

TEST(Http2HeaderBlockTest, Equality) {
  // Test equality and inequality operators.
  Http2HeaderBlock block1;
  block1["foo"] = "bar";

  Http2HeaderBlock block2;
  block2["foo"] = "bar";

  Http2HeaderBlock block3;
  block3["baz"] = "qux";

  EXPECT_EQ(block1, block2);
  EXPECT_NE(block1, block3);

  block2["baz"] = "qux";
  EXPECT_NE(block1, block2);
}

Http2HeaderBlock ReturnTestHeaderBlock() {
  Http2HeaderBlock block;
  block["foo"] = "bar";
  block.insert(std::make_pair("foo2", "baz"));
  return block;
}

// Test that certain methods do not crash on moved-from instances.
TEST(Http2HeaderBlockTest, MovedFromIsValid) {
  Http2HeaderBlock block1;
  block1["foo"] = "bar";

  Http2HeaderBlock block2(std::move(block1));
  EXPECT_THAT(block2, ElementsAre(Pair("foo", "bar")));

  block1["baz"] = "qux";  // NOLINT  testing post-move behavior

  Http2HeaderBlock block3(std::move(block1));

  block1["foo"] = "bar";  // NOLINT  testing post-move behavior

  Http2HeaderBlock block4(std::move(block1));

  block1.clear();  // NOLINT  testing post-move behavior
  EXPECT_TRUE(block1.empty());

  block1["foo"] = "bar";
  EXPECT_THAT(block1, ElementsAre(Pair("foo", "bar")));

  Http2HeaderBlock block5 = ReturnTestHeaderBlock();
  block5.AppendValueOrAddHeader("foo", "bar2");
  EXPECT_THAT(block5, ElementsAre(Pair("foo", std::string("bar\0bar2", 8)),
                                  Pair("foo2", "baz")));
}

// This test verifies that headers can be appended to no matter how they were
// added originally.
TEST(Http2HeaderBlockTest, AppendHeaders) {
  Http2HeaderBlock block;
  block["foo"] = "foo";
  block.AppendValueOrAddHeader("foo", "bar");
  EXPECT_EQ(Pair("foo", std::string("foo\0bar", 7)), *block.find("foo"));

  block.insert(std::make_pair("foo", "baz"));
  EXPECT_EQ("baz", block["foo"]);
  EXPECT_EQ(Pair("foo", "baz"), *block.find("foo"));

  // Try all four methods of adding an entry.
  block["cookie"] = "key1=value1";
  block.AppendValueOrAddHeader("h1", "h1v1");
  block.insert(std::make_pair("h2", "h2v1"));

  block.AppendValueOrAddHeader("h3", "h3v2");
  block.AppendValueOrAddHeader("h2", "h2v2");
  block.AppendValueOrAddHeader("h1", "h1v2");
  block.AppendValueOrAddHeader("cookie", "key2=value2");

  block.AppendValueOrAddHeader("cookie", "key3=value3");
  block.AppendValueOrAddHeader("h1", "h1v3");
  block.AppendValueOrAddHeader("h2", "h2v3");
  block.AppendValueOrAddHeader("h3", "h3v3");
  block.AppendValueOrAddHeader("h4", "singleton");

  EXPECT_EQ("key1=value1; key2=value2; key3=value3", block["cookie"]);
  EXPECT_EQ("baz", block["foo"]);
  EXPECT_EQ(std::string("h1v1\0h1v2\0h1v3", 14), block["h1"]);
  EXPECT_EQ(std::string("h2v1\0h2v2\0h2v3", 14), block["h2"]);
  EXPECT_EQ(std::string("h3v2\0h3v3", 9), block["h3"]);
  EXPECT_EQ("singleton", block["h4"]);
}

TEST(Http2HeaderBlockTest, CompareValueToStringPiece) {
  Http2HeaderBlock block;
  block["foo"] = "foo";
  block.AppendValueOrAddHeader("foo", "bar");
  const auto& val = block["foo"];
  const char expected[] = "foo\0bar";
  EXPECT_TRUE(absl::string_view(expected, 7) == val);
  EXPECT_TRUE(val == absl::string_view(expected, 7));
  EXPECT_FALSE(absl::string_view(expected, 3) == val);
  EXPECT_FALSE(val == absl::string_view(expected, 3));
  const char not_expected[] = "foo\0barextra";
  EXPECT_FALSE(absl::string_view(not_expected, 12) == val);
  EXPECT_FALSE(val == absl::string_view(not_expected, 12));

  const auto& val2 = block["foo2"];
  EXPECT_FALSE(absl::string_view(expected, 7) == val2);
  EXPECT_FALSE(val2 == absl::string_view(expected, 7));
  EXPECT_FALSE(absl::string_view("") == val2);
  EXPECT_FALSE(val2 == absl::string_view(""));
}

// This test demonstrates that the Http2HeaderBlock data structure does not
// place any limitations on the characters present in the header names.
TEST(Http2HeaderBlockTest, UpperCaseNames) {
  Http2HeaderBlock block;
  block["Foo"] = "foo";
  block.AppendValueOrAddHeader("Foo", "bar");
  EXPECT_NE(block.end(), block.find("foo"));
  EXPECT_EQ(Pair("Foo", std::string("foo\0bar", 7)), *block.find("Foo"));

  // The map is case insensitive, so updating "foo" modifies the entry
  // previously added.
  block.AppendValueOrAddHeader("foo", "baz");
  EXPECT_THAT(block,
              ElementsAre(Pair("Foo", std::string("foo\0bar\0baz", 11))));
}

namespace {
size_t Http2HeaderBlockSize(const Http2HeaderBlock& block) {
  size_t size = 0;
  for (const auto& pair : block) {
    size += pair.first.size() + pair.second.size();
  }
  return size;
}
}  // namespace

// Tests Http2HeaderBlock SizeEstimate().
TEST(Http2HeaderBlockTest, TotalBytesUsed) {
  Http2HeaderBlock block;
  const size_t value_size = 300;
  block["foo"] = std::string(value_size, 'x');
  EXPECT_EQ(block.TotalBytesUsed(), Http2HeaderBlockSize(block));
  block.insert(std::make_pair("key", std::string(value_size, 'x')));
  EXPECT_EQ(block.TotalBytesUsed(), Http2HeaderBlockSize(block));
  block.AppendValueOrAddHeader("abc", std::string(value_size, 'x'));
  EXPECT_EQ(block.TotalBytesUsed(), Http2HeaderBlockSize(block));

  // Replace value for existing key.
  block["foo"] = std::string(value_size, 'x');
  EXPECT_EQ(block.TotalBytesUsed(), Http2HeaderBlockSize(block));
  block.insert(std::make_pair("key", std::string(value_size, 'x')));
  EXPECT_EQ(block.TotalBytesUsed(), Http2HeaderBlockSize(block));
  // Add value for existing key.
  block.AppendValueOrAddHeader("abc", std::string(value_size, 'x'));
  EXPECT_EQ(block.TotalBytesUsed(), Http2HeaderBlockSize(block));

  // Copies/clones Http2HeaderBlock.
  size_t block_size = block.TotalBytesUsed();
  Http2HeaderBlock block_copy = std::move(block);
  EXPECT_EQ(block_size, block_copy.TotalBytesUsed());

  // Erases key.
  block_copy.erase("foo");
  EXPECT_EQ(block_copy.TotalBytesUsed(), Http2HeaderBlockSize(block_copy));
  block_copy.erase("key");
  EXPECT_EQ(block_copy.TotalBytesUsed(), Http2HeaderBlockSize(block_copy));
  block_copy.erase("abc");
  EXPECT_EQ(block_copy.TotalBytesUsed(), Http2HeaderBlockSize(block_copy));
}

}  // namespace test
}  // namespace spdy
