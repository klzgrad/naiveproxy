#ifndef QUICHE_OBLIVIOUS_HTTP_COMMON_OBLIVIOUS_HTTP_DEFINITIONS_H_
#define QUICHE_OBLIVIOUS_HTTP_COMMON_OBLIVIOUS_HTTP_DEFINITIONS_H_

#include <stdint.h>

namespace quiche {
// This 5-byte array represents the string "final", which is used as AD when
// encrypting/decrypting the final OHTTP chunk. See sections 6.1 and 6.2 of
// https://www.ietf.org/archive/id/draft-ietf-ohai-chunked-ohttp-05.html
constexpr uint8_t kFinalAdBytes[] = {0x66, 0x69, 0x6E, 0x61, 0x6C};

}  // namespace quiche

#endif  // QUICHE_OBLIVIOUS_HTTP_COMMON_OBLIVIOUS_HTTP_DEFINITIONS_H_
