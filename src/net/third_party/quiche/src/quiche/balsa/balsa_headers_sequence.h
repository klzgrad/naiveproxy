#ifndef QUICHE_BALSA_BALSA_HEADERS_SEQUENCE_H_
#define QUICHE_BALSA_BALSA_HEADERS_SEQUENCE_H_

#include <list>

#include "quiche/balsa/balsa_headers.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace quiche {

// Represents a sequence of BalsaHeaders. The sequence owns each BalsaHeaders,
// and the user asks for pointers to successive BalsaHeaders in the sequence.
class QUICHE_EXPORT BalsaHeadersSequence {
 public:
  // Appends `headers` to the end of the sequence.
  void Append(BalsaHeaders headers);

  // Returns true if there is a BalsaHeaders that has not yet been returned from
  // `Next()`. IFF true, `Next()` will return non-nullptr.
  bool HasNext() const;

  // Returns a non-owning pointer to the next BalsaHeaders in the sequence, or
  // nullptr if the next does not exist.
  BalsaHeaders* Next();

  // Similar to `Next()` but does not advance the sequence.
  // TODO(b/68801833): Consider removing after full refactoring is in place.
  BalsaHeaders* PeekNext() const;

  // Clears the sequence. Any previously returned BalsaHeaders become invalid.
  void Clear();

 private:
  std::list<BalsaHeaders> sequence_;
  std::list<BalsaHeaders>::iterator iter_ = sequence_.end();
};

}  // namespace quiche

#endif  // QUICHE_BALSA_BALSA_HEADERS_SEQUENCE_H_
