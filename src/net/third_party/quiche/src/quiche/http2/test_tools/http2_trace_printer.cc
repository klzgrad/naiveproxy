#include "quiche/http2/test_tools/http2_trace_printer.h"

#include <algorithm>
#include <cstddef>

#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "quiche/http2/core/http2_trace_logging.h"
#include "quiche/spdy/core/spdy_protocol.h"

namespace http2 {
namespace test {
namespace {

bool IsLoggingEnabled() { return true; }

}  // namespace

Http2TracePrinter::Http2TracePrinter(absl::string_view perspective,
                                     const void* connection_id,
                                     bool consume_connection_preface)
    : logger_(&visitor_, perspective, IsLoggingEnabled, connection_id),
      perspective_(perspective) {
  decoder_.set_visitor(&logger_);
  if (consume_connection_preface) {
    remaining_preface_ =
        absl::string_view(spdy::kHttp2ConnectionHeaderPrefix,
                          spdy::kHttp2ConnectionHeaderPrefixSize);
  }
}

void Http2TracePrinter::ProcessInput(absl::string_view bytes) {
  if (preface_error_) {
    HTTP2_TRACE_LOG(perspective_, IsLoggingEnabled)
        << "Earlier connection preface error, ignoring " << bytes.size()
        << " bytes";
    return;
  }
  if (!remaining_preface_.empty()) {
    const size_t consumed = std::min(remaining_preface_.size(), bytes.size());

    const absl::string_view preface = bytes.substr(0, consumed);
    HTTP2_TRACE_LOG(perspective_, IsLoggingEnabled)
        << "Received connection preface: " << absl::CEscape(preface);

    if (!absl::StartsWith(remaining_preface_, preface)) {
      HTTP2_TRACE_LOG(perspective_, IsLoggingEnabled)
          << "Received preface does not match expected remaining preface: "
          << absl::CEscape(remaining_preface_);
      preface_error_ = true;
      return;
    }
    bytes.remove_prefix(consumed);
    remaining_preface_.remove_prefix(consumed);
  }
  decoder_.ProcessInput(bytes.data(), bytes.size());
}

}  // namespace test
}  // namespace http2
