#ifndef QUICHE_HTTP2_ADAPTER_DATA_SOURCE_H_
#define QUICHE_HTTP2_ADAPTER_DATA_SOURCE_H_

#include <string>

#include "absl/strings/string_view.h"

namespace http2 {
namespace adapter {

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
