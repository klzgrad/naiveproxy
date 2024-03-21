#include "quiche/common/quiche_random.h"

#include <cstdint>
#include <cstring>

#include "openssl/rand.h"
#include "quiche/common/platform/api/quiche_logging.h"
namespace quiche {

namespace {

// Insecure randomness in DefaultRandom uses an implementation of
// xoshiro256++ 1.0 based on code in the public domain from
// <http://prng.di.unimi.it/xoshiro256plusplus.c>.

inline uint64_t Xoshiro256InitializeRngStateMember() {
  uint64_t result;
  RAND_bytes(reinterpret_cast<uint8_t*>(&result), sizeof(result));
  return result;
}

inline uint64_t Xoshiro256PlusPlusRotLeft(uint64_t x, int k) {
  return (x << k) | (x >> (64 - k));
}

uint64_t Xoshiro256PlusPlus() {
  static thread_local uint64_t rng_state[4] = {
      Xoshiro256InitializeRngStateMember(),
      Xoshiro256InitializeRngStateMember(),
      Xoshiro256InitializeRngStateMember(),
      Xoshiro256InitializeRngStateMember()};
  const uint64_t result =
      Xoshiro256PlusPlusRotLeft(rng_state[0] + rng_state[3], 23) + rng_state[0];
  const uint64_t t = rng_state[1] << 17;
  rng_state[2] ^= rng_state[0];
  rng_state[3] ^= rng_state[1];
  rng_state[1] ^= rng_state[2];
  rng_state[0] ^= rng_state[3];
  rng_state[2] ^= t;
  rng_state[3] = Xoshiro256PlusPlusRotLeft(rng_state[3], 45);
  return result;
}

class DefaultQuicheRandom : public QuicheRandom {
 public:
  DefaultQuicheRandom() {}
  DefaultQuicheRandom(const DefaultQuicheRandom&) = delete;
  DefaultQuicheRandom& operator=(const DefaultQuicheRandom&) = delete;
  ~DefaultQuicheRandom() override {}

  // QuicRandom implementation
  void RandBytes(void* data, size_t len) override;
  uint64_t RandUint64() override;
  void InsecureRandBytes(void* data, size_t len) override;
  uint64_t InsecureRandUint64() override;
};

void DefaultQuicheRandom::RandBytes(void* data, size_t len) {
  RAND_bytes(reinterpret_cast<uint8_t*>(data), len);
}

uint64_t DefaultQuicheRandom::RandUint64() {
  uint64_t value;
  RandBytes(&value, sizeof(value));
  return value;
}

void DefaultQuicheRandom::InsecureRandBytes(void* data, size_t len) {
  while (len >= sizeof(uint64_t)) {
    uint64_t random_bytes64 = Xoshiro256PlusPlus();
    memcpy(data, &random_bytes64, sizeof(uint64_t));
    data = reinterpret_cast<char*>(data) + sizeof(uint64_t);
    len -= sizeof(uint64_t);
  }
  if (len > 0) {
    QUICHE_DCHECK_LT(len, sizeof(uint64_t));
    uint64_t random_bytes64 = Xoshiro256PlusPlus();
    memcpy(data, &random_bytes64, len);
  }
}

uint64_t DefaultQuicheRandom::InsecureRandUint64() {
  return Xoshiro256PlusPlus();
}

}  // namespace

// static
QuicheRandom* QuicheRandom::GetInstance() {
  static DefaultQuicheRandom* random = new DefaultQuicheRandom();
  return random;
}
}  // namespace quiche
