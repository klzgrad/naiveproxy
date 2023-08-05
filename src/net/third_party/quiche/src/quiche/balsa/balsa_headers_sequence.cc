#include "quiche/balsa/balsa_headers_sequence.h"

#include <iterator>

#include "quiche/balsa/balsa_headers.h"

namespace quiche {

void BalsaHeadersSequence::Append(BalsaHeaders headers) {
  sequence_.push_back(std::move(headers));

  if (iter_ == sequence_.end()) {
    iter_ = std::prev(sequence_.end());
  }
}

bool BalsaHeadersSequence::HasNext() const { return iter_ != sequence_.end(); }

BalsaHeaders* BalsaHeadersSequence::PeekNext() const {
  if (!HasNext()) {
    return nullptr;
  }
  return &*iter_;
}

BalsaHeaders* BalsaHeadersSequence::Next() {
  if (!HasNext()) {
    return nullptr;
  }
  return &*iter_++;
}

void BalsaHeadersSequence::Clear() {
  sequence_.clear();
  iter_ = sequence_.end();
}

}  // namespace quiche
