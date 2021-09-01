#ifndef QUICHE_HTTP2_ADAPTER_DATA_SOURCE_H_
#define QUICHE_HTTP2_ADAPTER_DATA_SOURCE_H_

#include <string>
#include <utility>

#include "absl/strings/string_view.h"

namespace http2 {
namespace adapter {

// Represents a source of DATA frames for transmission to the peer.
class DataFrameSource {
 public:
  virtual ~DataFrameSource() {}

  static constexpr ssize_t kBlocked = 0;
  static constexpr ssize_t kError = -1;

  // Returns the number of bytes to send in the next DATA frame, and whether
  // this frame indicates the end of the data. Returns {kBlocked, false} if
  // blocked, {kError, false} on error.
  virtual std::pair<ssize_t, bool> SelectPayloadLength(size_t max_length) = 0;

  // This method is called with a frame header and a payload length to send. The
  // source should send or buffer the entire frame and return true, or return
  // false without sending or buffering anything.
  virtual bool Send(absl::string_view frame_header, size_t payload_length) = 0;

  // If true, the end of this data source indicates the end of the stream.
  // Otherwise, this data will be followed by trailers.
  virtual bool send_fin() const = 0;
};

// Represents a HTTP message body.
class DataSource {
 public:
  virtual ~DataSource() {}

  enum State {
    // The source is not done, but cannot currently provide more data.
    NOT_READY,
    // The source can provide more data.
    READY,
    // The source is done.
    DONE,
  };

  State state() const { return state_; }

  // The next range of data provided by this data source.
  virtual absl::string_view NextData() const = 0;

  // Indicates that |bytes| bytes have been consumed by the caller.
  virtual void Consume(size_t bytes) = 0;

 protected:
  State state_ = NOT_READY;
};

// A simple implementation constructible from a string_view or std::string.
class StringDataSource : public DataSource {
 public:
  explicit StringDataSource(std::string data);

  absl::string_view NextData() const override;
  void Consume(size_t bytes) override;

 private:
  const std::string data_;
  absl::string_view remaining_;
};

}  // namespace adapter
}  // namespace http2

#endif  // QUICHE_HTTP2_ADAPTER_DATA_SOURCE_H_
