#include "net/third_party/quiche/src/spdy/core/spdy_header_storage.h"

#include "net/third_party/quiche/src/common/platform/api/quiche_test.h"

namespace spdy {
namespace test {

TEST(JoinTest, JoinEmpty) {
  std::vector<quiche::QuicheStringPiece> empty;
  quiche::QuicheStringPiece separator = ", ";
  char buf[10] = "";
  size_t written = Join(buf, empty, separator);
  EXPECT_EQ(0u, written);
}

TEST(JoinTest, JoinOne) {
  std::vector<quiche::QuicheStringPiece> v = {"one"};
  quiche::QuicheStringPiece separator = ", ";
  char buf[15];
  size_t written = Join(buf, v, separator);
  EXPECT_EQ(3u, written);
  EXPECT_EQ("one", quiche::QuicheStringPiece(buf, written));
}

TEST(JoinTest, JoinMultiple) {
  std::vector<quiche::QuicheStringPiece> v = {"one", "two", "three"};
  quiche::QuicheStringPiece separator = ", ";
  char buf[15];
  size_t written = Join(buf, v, separator);
  EXPECT_EQ(15u, written);
  EXPECT_EQ("one, two, three", quiche::QuicheStringPiece(buf, written));
}

}  // namespace test
}  // namespace spdy
