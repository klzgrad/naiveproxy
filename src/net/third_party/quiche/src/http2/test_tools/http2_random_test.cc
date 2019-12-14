#include "net/third_party/quiche/src/http2/test_tools/http2_random.h"

#include <set>

#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace http2 {
namespace test {
namespace {

TEST(Http2RandomTest, ProducesDifferentNumbers) {
  Http2Random random;
  uint64_t value1 = random.Rand64();
  uint64_t value2 = random.Rand64();
  uint64_t value3 = random.Rand64();

  EXPECT_NE(value1, value2);
  EXPECT_NE(value2, value3);
  EXPECT_NE(value3, value1);
}

TEST(Http2RandomTest, StartsWithDifferentKeys) {
  Http2Random random1;
  Http2Random random2;

  EXPECT_NE(random1.Key(), random2.Key());
  EXPECT_NE(random1.Rand64(), random2.Rand64());
  EXPECT_NE(random1.Rand64(), random2.Rand64());
  EXPECT_NE(random1.Rand64(), random2.Rand64());
}

TEST(Http2RandomTest, ReproducibleRandom) {
  Http2Random random;
  uint64_t value1 = random.Rand64();
  uint64_t value2 = random.Rand64();

  Http2Random clone_random(random.Key());
  EXPECT_EQ(clone_random.Key(), random.Key());
  EXPECT_EQ(value1, clone_random.Rand64());
  EXPECT_EQ(value2, clone_random.Rand64());
}

TEST(Http2RandomTest, STLShuffle) {
  Http2Random random;
  const std::string original = "abcdefghijklmonpqrsuvwxyz";

  std::string shuffled = original;
  std::shuffle(shuffled.begin(), shuffled.end(), random);
  EXPECT_NE(original, shuffled);
}

TEST(Http2RandomTest, RandFloat) {
  Http2Random random;
  for (int i = 0; i < 10000; i++) {
    float value = random.RandFloat();
    ASSERT_GE(value, 0.f);
    ASSERT_LE(value, 1.f);
  }
}

TEST(Http2RandomTest, RandStringWithAlphabet) {
  Http2Random random;
  std::string str = random.RandStringWithAlphabet(1000, "xyz");
  EXPECT_EQ(1000u, str.size());

  std::set<char> characters(str.begin(), str.end());
  EXPECT_THAT(characters, testing::ElementsAre('x', 'y', 'z'));
}

TEST(Http2RandomTest, SkewedLow) {
  Http2Random random;
  constexpr size_t kMax = 1234;
  for (int i = 0; i < 10000; i++) {
    size_t value = random.RandomSizeSkewedLow(kMax);
    ASSERT_GE(value, 0u);
    ASSERT_LE(value, kMax);
  }
}

// Checks that SkewedLow() generates full range.  This is required, since in
// some unit tests would infinitely loop.
TEST(Http2RandomTest, SkewedLowFullRange) {
  Http2Random random;
  std::set<size_t> values;
  for (int i = 0; i < 1000; i++) {
    values.insert(random.RandomSizeSkewedLow(3));
  }
  EXPECT_THAT(values, testing::ElementsAre(0, 1, 2, 3));
}

}  // namespace
}  // namespace test
}  // namespace http2
