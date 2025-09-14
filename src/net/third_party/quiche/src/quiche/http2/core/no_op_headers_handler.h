#ifndef QUICHE_HTTP2_CORE_NO_OP_HEADERS_HANDLER_H_
#define QUICHE_HTTP2_CORE_NO_OP_HEADERS_HANDLER_H_

#include <cstddef>

#include "absl/strings/string_view.h"
#include "quiche/http2/core/header_byte_listener_interface.h"
#include "quiche/http2/core/spdy_headers_handler_interface.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace spdy {

// Drops all header data, but passes information about header bytes parsed to
// a listener.
class QUICHE_EXPORT NoOpHeadersHandler : public SpdyHeadersHandlerInterface {
 public:
  // Does not take ownership of listener.
  explicit NoOpHeadersHandler(HeaderByteListenerInterface* listener)
      : listener_(listener) {}
  NoOpHeadersHandler(const NoOpHeadersHandler&) = delete;
  NoOpHeadersHandler& operator=(const NoOpHeadersHandler&) = delete;
  ~NoOpHeadersHandler() override {}

  // From SpdyHeadersHandlerInterface
  void OnHeaderBlockStart() override {}
  void OnHeader(absl::string_view /*key*/,
                absl::string_view /*value*/) override {}
  void OnHeaderBlockEnd(size_t uncompressed_header_bytes,
                        size_t /* compressed_header_bytes */) override {
    if (listener_ != nullptr) {
      listener_->OnHeaderBytesReceived(uncompressed_header_bytes);
    }
  }

 private:
  HeaderByteListenerInterface* listener_;
};

}  // namespace spdy

#endif  // QUICHE_HTTP2_CORE_NO_OP_HEADERS_HANDLER_H_
