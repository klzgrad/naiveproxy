// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/spdy/core/spdy_header_block.h"

#include <memory>
#include <utility>

#include "net/third_party/quiche/src/spdy/core/spdy_test_utils.h"
#include "net/third_party/quiche/src/spdy/platform/api/spdy_test.h"

using ::testing::ElementsAre;

namespace spdy {
namespace test {

class ValueProxyPeer {
 public:
  static SpdyStringPiece key(SpdyHeaderBlock::ValueProxy* p) { return p->key_; }
};

std::pair<SpdyStringPiece, SpdyStringPiece> Pair(SpdyStringPiece k,
                                                 SpdyStringPiece v) {
  return std::make_pair(k, v);
}

// This test verifies that SpdyHeaderBlock behaves correctly when empty.
TEST(SpdyHeaderBlockTest, EmptyBlock) {
  SpdyHeaderBlock block;
  EXPECT_TRUE(block.empty());
  EXPECT_EQ(0u, block.size());
  EXPECT_EQ(block.end(), block.find("foo"));
  EXPECT_TRUE(block.end() == block.begin());

  // Should have no effect.
  block.erase("bar");
}

TEST(SpdyHeaderBlockTest, KeyMemoryReclaimedOnLookup) {
  SpdyHeaderBlock block;
  SpdyStringPiece copied_key1;
  {
    auto proxy1 = block["some key name"];
    copied_key1 = ValueProxyPeer::key(&proxy1);
  }
  SpdyStringPiece copied_key2;
  {
    auto proxy2 = block["some other key name"];
    copied_key2 = ValueProxyPeer::key(&proxy2);
  }
  // Because proxy1 was never used to modify the block, the memory used for the
  // key could be reclaimed and used for the second call to operator[].
  // Therefore, we expect the pointers of the two SpdyStringPieces to be equal.
  EXPECT_EQ(copied_key1.data(), copied_key2.data());

  {
    auto proxy1 = block["some key name"];
    block["some other key name"] = "some value";
  }
  // Nothing should blow up when proxy1 is destructed, and we should be able to
  // modify and access the SpdyHeaderBlock.
  block["key"] = "value";
  EXPECT_EQ("value", block["key"]);
  EXPECT_EQ("some value", block["some other key name"]);
  EXPECT_TRUE(block.find("some key name") == block.end());
}

// This test verifies that headers can be set in a variety of ways.
TEST(SpdyHeaderBlockTest, AddHeaders) {
  SpdyHeaderBlock block;
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

// This test verifies that SpdyHeaderBlock can be copied using Clone().
TEST(SpdyHeaderBlockTest, CopyBlocks) {
  SpdyHeaderBlock block1;
  block1["foo"] = std::string(300, 'x');
  block1["bar"] = "baz";
  block1.insert(std::make_pair("qux", "qux1"));

  SpdyHeaderBlock block2 = block1.Clone();
  SpdyHeaderBlock block3(block1.Clone());

  EXPECT_EQ(block1, block2);
  EXPECT_EQ(block1, block3);
}

TEST(SpdyHeaderBlockTest, Equality) {
  // Test equality and inequality operators.
  SpdyHeaderBlock block1;
  block1["foo"] = "bar";

  SpdyHeaderBlock block2;
  block2["foo"] = "bar";

  SpdyHeaderBlock block3;
  block3["baz"] = "qux";

  EXPECT_EQ(block1, block2);
  EXPECT_NE(block1, block3);

  block2["baz"] = "qux";
  EXPECT_NE(block1, block2);
}

// Test that certain methods do not crash on moved-from instances.
TEST(SpdyHeaderBlockTest, MovedFromIsValid) {
  SpdyHeaderBlock block1;
  block1["foo"] = "bar";

  SpdyHeaderBlock block2(std::move(block1));
  EXPECT_THAT(block2, ElementsAre(Pair("foo", "bar")));

  block1["baz"] = "qux";  // NOLINT  testing post-move behavior

  SpdyHeaderBlock block3(std::move(block1));

  block1["foo"] = "bar";  // NOLINT  testing post-move behavior

  SpdyHeaderBlock block4(std::move(block1));

  block1.clear();  // NOLINT  testing post-move behavior
  EXPECT_TRUE(block1.empty());

  block1["foo"] = "bar";
  EXPECT_THAT(block1, ElementsAre(Pair("foo", "bar")));
}

// This test verifies that headers can be appended to no matter how they were
// added originally.
TEST(SpdyHeaderBlockTest, AppendHeaders) {
  SpdyHeaderBlock block;
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

TEST(JoinTest, JoinEmpty) {
  std::vector<SpdyStringPiece> empty;
  SpdyStringPiece separator = ", ";
  char buf[10] = "";
  size_t written = Join(buf, empty, separator);
  EXPECT_EQ(0u, written);
}

TEST(JoinTest, JoinOne) {
  std::vector<SpdyStringPiece> v = {"one"};
  SpdyStringPiece separator = ", ";
  char buf[15];
  size_t written = Join(buf, v, separator);
  EXPECT_EQ(3u, written);
  EXPECT_EQ("one", SpdyStringPiece(buf, written));
}

TEST(JoinTest, JoinMultiple) {
  std::vector<SpdyStringPiece> v = {"one", "two", "three"};
  SpdyStringPiece separator = ", ";
  char buf[15];
  size_t written = Join(buf, v, separator);
  EXPECT_EQ(15u, written);
  EXPECT_EQ("one, two, three", SpdyStringPiece(buf, written));
}

namespace {
size_t SpdyHeaderBlockSize(const SpdyHeaderBlock& block) {
  size_t size = 0;
  for (const auto& pair : block) {
    size += pair.first.size() + pair.second.size();
  }
  return size;
}
}  // namespace

// Tests SpdyHeaderBlock SizeEstimate().
TEST(SpdyHeaderBlockTest, TotalBytesUsed) {
  SpdyHeaderBlock block;
  const size_t value_size = 300;
  block["foo"] = std::string(value_size, 'x');
  EXPECT_EQ(block.TotalBytesUsed(), SpdyHeaderBlockSize(block));
  block.insert(std::make_pair("key", std::string(value_size, 'x')));
  EXPECT_EQ(block.TotalBytesUsed(), SpdyHeaderBlockSize(block));
  block.AppendValueOrAddHeader("abc", std::string(value_size, 'x'));
  EXPECT_EQ(block.TotalBytesUsed(), SpdyHeaderBlockSize(block));

  // Replace value for existing key.
  block["foo"] = std::string(value_size, 'x');
  EXPECT_EQ(block.TotalBytesUsed(), SpdyHeaderBlockSize(block));
  block.insert(std::make_pair("key", std::string(value_size, 'x')));
  EXPECT_EQ(block.TotalBytesUsed(), SpdyHeaderBlockSize(block));
  // Add value for existing key.
  block.AppendValueOrAddHeader("abc", std::string(value_size, 'x'));
  EXPECT_EQ(block.TotalBytesUsed(), SpdyHeaderBlockSize(block));

  // Copies/clones SpdyHeaderBlock.
  size_t block_size = block.TotalBytesUsed();
  SpdyHeaderBlock block_copy = std::move(block);
  EXPECT_EQ(block_size, block_copy.TotalBytesUsed());

  // Erases key.
  block_copy.erase("foo");
  EXPECT_EQ(block_copy.TotalBytesUsed(), SpdyHeaderBlockSize(block_copy));
  block_copy.erase("key");
  EXPECT_EQ(block_copy.TotalBytesUsed(), SpdyHeaderBlockSize(block_copy));
  block_copy.erase("abc");
  EXPECT_EQ(block_copy.TotalBytesUsed(), SpdyHeaderBlockSize(block_copy));
}

}  // namespace test
}  // namespace spdy
