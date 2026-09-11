/*
 * Copyright (C) 2025 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_PERFETTO_SQL_SYNTAQLITE_UTILS_H_
#define SRC_TRACE_PROCESSOR_PERFETTO_SQL_SYNTAQLITE_UTILS_H_

#include <cstdint>
#include <string_view>

#include "src/trace_processor/perfetto_sql/syntaqlite/syntaqlite_perfetto.h"

namespace perfetto::trace_processor {

// Returns the authored source text for |span| as a string_view into the
// original input — no allocation. Uses span_text (not expanded_text) so
// macro-expanded spans resolve back to the call-site source.
inline std::string_view SyntaqliteSpanText(SyntaqliteParser* p,
                                           SyntaqliteTextSpan span) {
  uint32_t len = 0;
  const char* text = syntaqlite_parser_span_text(p, &span, &len, nullptr);
  if (!text) {
    return {};
  }
  return {text, len};
}

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_PERFETTO_SQL_SYNTAQLITE_UTILS_H_
