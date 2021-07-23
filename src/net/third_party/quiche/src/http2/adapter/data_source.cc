#include "http2/adapter/data_source.h"

namespace http2 {
namespace adapter {

StringDataSource::StringDataSource(std::string data)
    : data_(std::move(data)), remaining_(data_) {
  state_ = remaining_.empty() ? DONE : READY;
}

absl::string_view StringDataSource::NextData() const {
  return remaining_;
}

void StringDataSource::Consume(size_t bytes) {
  remaining_.remove_prefix(std::min(bytes, remaining_.size()));
  if (remaining_.empty()) {
    state_ = DONE;
  }
}

}  // namespace adapter
}  // namespace http2
