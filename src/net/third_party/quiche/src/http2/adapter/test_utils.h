#ifndef QUICHE_HTTP2_ADAPTER_TEST_UTILS_H_
#define QUICHE_HTTP2_ADAPTER_TEST_UTILS_H_

#include <string>
#include <vector>

#include "common/platform/api/quiche_test.h"
#include "spdy/core/spdy_protocol.h"

namespace http2 {
namespace adapter {
namespace test {

// Matcher that checks whether a string contains HTTP/2 frames of the specified
// ordered sequence of types and lengths.
testing::Matcher<const std::string> ContainsFrames(
    std::vector<std::pair<spdy::SpdyFrameType, absl::optional<size_t>>>
        types_and_lengths);

// Matcher that checks whether a string contains HTTP/2 frames of the specified
// ordered sequence of types.
testing::Matcher<const std::string> ContainsFrames(
    std::vector<spdy::SpdyFrameType> types);

}  // namespace test
}  // namespace adapter
}  // namespace http2

#endif  // QUICHE_HTTP2_ADAPTER_TEST_UTILS_H_
