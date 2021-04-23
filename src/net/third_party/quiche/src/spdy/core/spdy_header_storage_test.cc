#include "spdy/core/spdy_header_storage.h"

#include "common/platform/api/quiche_test.h"

namespace spdy {
namespace test {

TEST(JoinTest, JoinEmpty) {
  std::vector<absl::string_view> empty;
  absl::string_view separator = ", ";
  char buf[10] = "";
  size_t written = Join(buf, empty, separator);
  EXPECT_EQ(0u, written);
}

TEST(JoinTest, JoinOne) {
  std::vector<absl::string_view> v = {"one"};
  absl::string_view separator = ", ";
  char buf[15];
  size_t written = Join(buf, v, separator);
  EXPECT_EQ(3u, written);
  EXPECT_EQ("one", absl::string_view(buf, written));
}

TEST(JoinTest, JoinMultiple) {
  std::vector<absl::string_view> v = {"one", "two", "three"};
  absl::string_view separator = ", ";
  char buf[15];
  size_t written = Join(buf, v, separator);
  EXPECT_EQ(15u, written);
  EXPECT_EQ("one, two, three", absl::string_view(buf, written));
}

}  // namespace test
}  // namespace spdy
