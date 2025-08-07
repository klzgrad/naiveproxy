#include "quiche/http2/test_tools/http2_random.h"

#include <string>

#include "absl/strings/escaping.h"
#include "openssl/chacha.h"
#include "openssl/rand.h"
#include "quiche/common/platform/api/quiche_logging.h"

static const uint8_t kZeroNonce[12] = {0};

namespace http2 {
namespace test {

Http2Random::Http2Random() {
  RAND_bytes(key_, sizeof(key_));

  QUICHE_LOG(INFO) << "Initialized test RNG with the following key: " << Key();
}

Http2Random::Http2Random(absl::string_view key) {
  std::string decoded_key;
  QUICHE_CHECK(absl::HexStringToBytes(key, &decoded_key));
  QUICHE_CHECK_EQ(sizeof(key_), decoded_key.size());
  memcpy(key_, decoded_key.data(), sizeof(key_));
}

std::string Http2Random::Key() const {
  return absl::BytesToHexString(
      absl::string_view(reinterpret_cast<const char*>(key_), sizeof(key_)));
}

void Http2Random::FillRandom(void* buffer, size_t buffer_size) {
  memset(buffer, 0, buffer_size);
  uint8_t* buffer_u8 = reinterpret_cast<uint8_t*>(buffer);
  CRYPTO_chacha_20(buffer_u8, buffer_u8, buffer_size, key_, kZeroNonce,
                   counter_++);
}

std::string Http2Random::RandString(int length) {
  std::string result;
  result.resize(length);
  FillRandom(&result[0], length);
  return result;
}

uint64_t Http2Random::Rand64() {
  union {
    uint64_t number;
    uint8_t bytes[sizeof(uint64_t)];
  } result;
  FillRandom(result.bytes, sizeof(result.bytes));
  return result.number;
}

double Http2Random::RandDouble() {
  union {
    double f;
    uint64_t i;
  } value;
  value.i = (1023ull << 52ull) | (Rand64() & 0xfffffffffffffu);
  return value.f - 1.0;
}

std::string Http2Random::RandStringWithAlphabet(int length,
                                                absl::string_view alphabet) {
  std::string result;
  result.resize(length);
  for (int i = 0; i < length; i++) {
    result[i] = alphabet[Uniform(alphabet.size())];
  }
  return result;
}

}  // namespace test
}  // namespace http2
