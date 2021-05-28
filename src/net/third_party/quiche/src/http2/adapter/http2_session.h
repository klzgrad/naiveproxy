#ifndef QUICHE_HTTP2_ADAPTER_HTTP2_SESSION_H_
#define QUICHE_HTTP2_ADAPTER_HTTP2_SESSION_H_

#include <cstdint>

#include "absl/strings/string_view.h"
#include "http2/adapter/http2_protocol.h"

namespace http2 {
namespace adapter {

struct Http2SessionCallbacks {};

// A class to represent the state of a single HTTP/2 connection.
class Http2Session {
 public:
  Http2Session() = default;
  virtual ~Http2Session() {}

  virtual ssize_t ProcessBytes(absl::string_view bytes) = 0;

  virtual int Consume(Http2StreamId stream_id, size_t num_bytes) = 0;

  virtual bool want_read() const = 0;
  virtual bool want_write() const = 0;
  virtual int GetRemoteWindowSize() const = 0;
};

class Http2Options {
 public:
  Http2Options() = default;
  virtual ~Http2Options() {}

  // This method returns an opaque reference to the underlying type.
  virtual void* GetOptions() = 0;
};

}  // namespace adapter
}  // namespace http2

#endif  // QUICHE_HTTP2_ADAPTER_HTTP2_SESSION_H_
