/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "perfetto/ext/base/fixed_string_writer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace perfetto {
namespace base {

void FixedStringWriter::AppendHexString(const uint8_t* data,
                                        size_t size,
                                        char separator) {
  // Truncate to 64 bytes, as this is the maximum supported by the Linux
  // kernel's vsnprintf implementation.
  size_t printed_size = std::min<size_t>(size, size_t{64});
  // Remove trailing separator from calculation if printed_size > 0.
  size_t max_chars = printed_size * 3 - (printed_size > 0 ? 1 : 0);
  PERFETTO_DCHECK(pos_ + max_chars <= size_);

  if (printed_size) {
    AppendPaddedHexInt(data[0], '0', 2);
  }
  for (size_t pos = 1; pos < printed_size; pos++) {
    AppendChar(separator);
    AppendPaddedHexInt(data[pos], '0', 2);
  }
}

}  // namespace base
}  // namespace perfetto
