/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "perfetto/ext/base/uuid.h"

#include <random>

#include "perfetto/base/time.h"
#include "perfetto/ext/base/utils.h"

namespace perfetto {
namespace base {
namespace {

constexpr char kHexmap[] = {'0', '1', '2', '3', '4', '5', '6', '7',
                            '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

}  // namespace

// A globally unique 128-bit number.
// In the early days of perfetto we were (sorta) respecting rfc4122. Later we
// started replacing the LSB of the UUID with the statsd subscription ID in
// other parts of the codebase (see perfetto_cmd.cc) for the convenience of
// trace lookups, so rfc4122 made no sense as it just reduced entropy.
Uuid Uuidv4() {
  // Mix different sources of entropy to reduce the chances of collisions.
  // Only using boot time is not enough. Under the assumption that most traces
  // are started around the same time at boot, within a 1s window, the birthday
  // paradox gives a chance of 90% collisions with 70k traces over a 1e9 space
  // (Number of ns in a 1s window).
  // We deliberately don't use /dev/urandom as that might block for
  // unpredictable time if the system is idle (and is not portable).
  // The UUID does NOT need to be cryptographically secure, but random enough
  // to avoid collisions across a large number of devices.
  uint64_t boot_ns = static_cast<uint64_t>(GetBootTimeNs().count());
  uint64_t epoch_ns = static_cast<uint64_t>(GetWallTimeNs().count());

  // Use code ASLR as entropy source.
  uint32_t code_ptr =
      static_cast<uint32_t>(reinterpret_cast<uint64_t>(&Uuidv4) >> 12);

  // Use stack ASLR as a further entropy source.
  uint32_t stack_ptr =
      static_cast<uint32_t>(reinterpret_cast<uint64_t>(&code_ptr) >> 12);

  uint32_t entropy[] = {static_cast<uint32_t>(boot_ns >> 32),
                        static_cast<uint32_t>(boot_ns),
                        static_cast<uint32_t>(epoch_ns >> 32),
                        static_cast<uint32_t>(epoch_ns),
                        code_ptr,
                        stack_ptr};
  std::seed_seq entropy_seq(entropy, entropy + ArraySize(entropy));

  auto words = std::array<uint32_t, 4>();
  entropy_seq.generate(words.begin(), words.end());
  uint64_t msb = static_cast<uint64_t>(words[0]) << 32u | words[1];
  uint64_t lsb = static_cast<uint64_t>(words[2]) << 32u | words[3];
  return Uuid(static_cast<int64_t>(lsb), static_cast<int64_t>(msb));
}

Uuid::Uuid() {}

Uuid::Uuid(const std::string& s) {
  PERFETTO_CHECK(s.size() == data_.size());
  memcpy(data_.data(), s.data(), s.size());
}

Uuid::Uuid(int64_t lsb, int64_t msb) {
  set_lsb_msb(lsb, msb);
}

std::string Uuid::ToString() const {
  return std::string(reinterpret_cast<const char*>(data_.data()), data_.size());
}

std::string Uuid::ToPrettyString() const {
  std::string s(data_.size() * 2 + 4, '-');
  // Format is 123e4567-e89b-12d3-a456-426655443322.
  size_t j = 0;
  for (size_t i = 0; i < data_.size(); ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10)
      j++;
    s[2 * i + j] = kHexmap[(data_[data_.size() - i - 1] & 0xf0) >> 4];
    s[2 * i + 1 + j] = kHexmap[(data_[data_.size() - i - 1] & 0x0f)];
  }
  return s;
}

}  // namespace base
}  // namespace perfetto
