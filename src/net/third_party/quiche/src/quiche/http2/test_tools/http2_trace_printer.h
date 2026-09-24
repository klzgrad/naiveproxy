#ifndef QUICHE_HTTP2_TEST_TOOLS_HTTP2_TRACE_PRINTER_H_
#define QUICHE_HTTP2_TEST_TOOLS_HTTP2_TRACE_PRINTER_H_

#include <cstddef>

#include "absl/strings/string_view.h"
#include "quiche/http2/core/http2_frame_decoder_adapter.h"
#include "quiche/http2/core/http2_trace_logging.h"
#include "quiche/http2/core/spdy_no_op_visitor.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace http2 {
namespace test {

// A debugging utility that prints HTTP/2 wire bytes into logical HTTP/2 frame
// sequences using `Http2TraceLogger`.
class QUICHE_NO_EXPORT Http2TracePrinter {
 public:
  // Creates a printer with the given `perspective` prefixed with each log line
  // (e.g., "CLIENT" or "SERVER"). The given `connection_id` is also included
  // with each log line and distinguishes among multiple printed connections
  // with the same `perspective`. If `consume_connection_preface` is true, the
  // printer will attempt to consume and log the HTTP/2 client connection
  // preface from the wire bytes.
  explicit Http2TracePrinter(absl::string_view perspective,
                             const void* connection_id = nullptr,
                             bool consume_connection_preface = false);

  // Processes the `bytes` as HTTP/2 wire format and INFO logs the received
  // frames. See `Http2TraceLogger` for more details on the logging format. If
  // `consume_connection_preface` was passed as true to the constructor, then
  // errors in processing the connection preface will be logged and subsequent
  // calls to `ProcessInput()` will be a no-op.
  void ProcessInput(absl::string_view bytes);

 private:
  spdy::SpdyNoOpVisitor visitor_;
  Http2TraceLogger logger_;
  Http2DecoderAdapter decoder_;
  const absl::string_view perspective_;
  absl::string_view remaining_preface_;
  bool preface_error_ = false;
};

}  // namespace test
}  // namespace http2

#endif  // QUICHE_HTTP2_TEST_TOOLS_HTTP2_TRACE_PRINTER_H_
