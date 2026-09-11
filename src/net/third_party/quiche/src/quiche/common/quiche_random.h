#ifndef QUICHE_COMMON_QUICHE_RANDOM_H_
#define QUICHE_COMMON_QUICHE_RANDOM_H_

#include <cstddef>
#include <cstdint>

#include "quiche/common/platform/api/quiche_export.h"

namespace quiche {

// The interface for a random number generator.
class QUICHE_EXPORT QuicheRandom {
 public:
  virtual ~QuicheRandom() {}

  // Returns the default random number generator, which is cryptographically
  // secure and thread-safe.
  static QuicheRandom* GetInstance();

  // Generates |len| random bytes in the |data| buffer.
  virtual void RandBytes(void* data, size_t len) = 0;

  // Returns a random number in the range [0, kuint64max].
  virtual uint64_t RandUint64() = 0;

  // Generates |len| random bytes in the |data| buffer. This MUST NOT be used
  // for any application that requires cryptographically-secure randomness.
  virtual void InsecureRandBytes(void* data, size_t len) = 0;

  // Returns a random number in the range [0, kuint64max]. This MUST NOT be used
  // for any application that requires cryptographically-secure randomness.
  virtual uint64_t InsecureRandUint64() = 0;
};

}  // namespace quiche

#endif  // QUICHE_COMMON_QUICHE_RANDOM_H_
