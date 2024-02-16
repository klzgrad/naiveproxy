#ifndef QUICHE_BALSA_BALSA_HEADERS_SEQUENCE_H_
#define QUICHE_BALSA_BALSA_HEADERS_SEQUENCE_H_

#include <cstddef>
#include <memory>

#include "absl/container/inlined_vector.h"
#include "quiche/balsa/balsa_headers.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace quiche {

// Represents a sequence of BalsaHeaders. The sequence owns each BalsaHeaders,
// and the user asks for pointers to successive BalsaHeaders in the sequence.
class QUICHE_EXPORT BalsaHeadersSequence {
 public:
  // Appends `headers` to the end of the sequence.
  void Append(std::unique_ptr<BalsaHeaders> headers);

  // Returns true if there is a BalsaHeaders that has not yet been returned from
  // `Next()`. IFF true, `Next()` will return non-nullptr.
  bool HasNext() const;

  // Returns true if the sequence has no BalsaHeaders. It is possible to have
  // both !HasNext() and !IsEmpty() if all BalsaHeaders have been consumed.
  bool IsEmpty() const { return sequence_.empty(); }

  // Returns a non-owning pointer to the next BalsaHeaders in the sequence, or
  // nullptr if the next does not exist.
  BalsaHeaders* Next();

  // Similar to `Next()` but does not advance the sequence.
  // TODO(b/68801833): Consider removing after full refactoring is in place.
  BalsaHeaders* PeekNext();

  // Clears the sequence. Any previously returned BalsaHeaders become invalid.
  void Clear();

 private:
  // Typically at most two interim responses: an optional 100 Continue and an
  // optional 103 Early Hints.
  absl::InlinedVector<std::unique_ptr<BalsaHeaders>, 2> sequence_;

  // The index of the next entry in the sequence.
  size_t next_ = 0;
};

}  // namespace quiche

#endif  // QUICHE_BALSA_BALSA_HEADERS_SEQUENCE_H_
