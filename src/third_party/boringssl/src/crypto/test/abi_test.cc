/* Copyright (c) 2018, Google Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE. */

#include "abi_test.h"

#include <openssl/rand.h>


namespace abi_test {
namespace internal {

std::string FixVAArgsString(const char *str) {
  std::string ret = str;
  size_t idx = ret.find(',');
  if (idx == std::string::npos) {
    return ret + "()";
  }
  size_t idx2 = idx + 1;
  while (idx2 < ret.size() && ret[idx2] == ' ') {
    idx2++;
  }
  while (idx > 0 && ret[idx - 1] == ' ') {
    idx--;
  }
  return ret.substr(0, idx) + "(" + ret.substr(idx2) + ")";
}

#if defined(SUPPORTS_ABI_TEST)
crypto_word_t RunTrampoline(Result *out, crypto_word_t func,
                            const crypto_word_t *argv, size_t argc) {
  CallerState state;
  RAND_bytes(reinterpret_cast<uint8_t *>(&state), sizeof(state));

  // TODO(davidben): Use OS debugging APIs to single-step |func| and test that
  // CFI and SEH annotations are correct.
  CallerState state2 = state;
  crypto_word_t ret = abi_test_trampoline(func, &state2, argv, argc);

  *out = Result();
#define CALLER_STATE_REGISTER(type, name)                    \
  if (state.name != state2.name) {                           \
    out->errors.push_back(#name " was not restored"); \
  }
  LOOP_CALLER_STATE_REGISTERS()
#undef CALLER_STATE_REGISTER
  return ret;
}
#endif

}  // namespace internal
}  // namespace abi_test
