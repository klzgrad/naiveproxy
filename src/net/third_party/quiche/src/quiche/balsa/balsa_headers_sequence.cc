#include "quiche/balsa/balsa_headers_sequence.h"

#include <memory>

#include "quiche/balsa/balsa_headers.h"

namespace quiche {

void BalsaHeadersSequence::Append(std::unique_ptr<BalsaHeaders> headers) {
  sequence_.push_back(std::move(headers));
}

bool BalsaHeadersSequence::HasNext() const { return next_ < sequence_.size(); }

BalsaHeaders* BalsaHeadersSequence::PeekNext() {
  if (!HasNext()) {
    return nullptr;
  }
  return sequence_[next_].get();
}

BalsaHeaders* BalsaHeadersSequence::Next() {
  if (!HasNext()) {
    return nullptr;
  }
  return sequence_[next_++].get();
}

void BalsaHeadersSequence::Clear() {
  sequence_.clear();
  next_ = 0;
}

}  // namespace quiche
