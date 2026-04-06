/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "src/trace_processor/util/build_id.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"

namespace perfetto::trace_processor {
namespace {
uint8_t HexToBinary(char c) {
  switch (c) {
    case '0':
      return 0;
    case '1':
      return 1;
    case '2':
      return 2;
    case '3':
      return 3;
    case '4':
      return 4;
    case '5':
      return 5;
    case '6':
      return 6;
    case '7':
      return 7;
    case '8':
      return 8;
    case '9':
      return 9;
    case 'a':
    case 'A':
      return 10;
    case 'b':
    case 'B':
      return 11;
    case 'c':
    case 'C':
      return 12;
    case 'd':
    case 'D':
      return 13;
    case 'e':
    case 'E':
      return 14;
    case 'f':
    case 'F':
      return 15;
    default:
      PERFETTO_CHECK(false);
  }
}

std::string HexToBinary(base::StringView hex) {
  std::string res;
  res.reserve((hex.size() + 1) / 2);
  const auto* it = hex.begin();

  if (hex.size() % 2 != 0) {
    res.push_back(static_cast<char>(HexToBinary(*it)));
    ++it;
  }

  while (it != hex.end()) {
    if (*it == '-') {
      ++it;
      continue;
    }
    int v = (HexToBinary(*it++) << 4);
    v += HexToBinary(*it++);
    res.push_back(static_cast<char>(v));
  }
  return res;
}

// Returns whether this string is of a hex chrome module or not to decide
// whether to convert the module to/from hex.
// TODO(b/148109467): Remove workaround once all active Chrome versions
// write raw bytes instead of a string as build_id.
bool IsHexModuleId(base::StringView module) {
  return module.size() == 33;
}

}  // namespace

// static
BuildId BuildId::FromHex(base::StringView data) {
  if (IsHexModuleId(data)) {
    return BuildId(data.ToStdString());
  }
  return BuildId(HexToBinary(data));
}

std::string BuildId::ToHex() const {
  if (IsHexModuleId(base::StringView(raw_))) {
    return raw_;
  }
  return base::ToHex(raw_.data(), raw_.size());
}

}  // namespace perfetto::trace_processor
