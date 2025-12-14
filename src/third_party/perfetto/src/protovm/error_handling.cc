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

#include "src/protovm/error_handling.h"

namespace perfetto {
namespace protovm {

void LogStacktraceMessage(Stacktrace& stacktrace,
                          const char* file_name,
                          int file_line,
                          const char* fmt,
                          ...) {
  const char* const fmt_file_info = "%s:%d ";
  auto size_file_info =
      std::snprintf(nullptr, 0, fmt_file_info, file_name, file_line);

  va_list args;
  va_start(args, fmt);
  auto size_message = std::vsnprintf(nullptr, 0, fmt, args);
  va_end(args);

  auto s = std::string(
      static_cast<std::size_t>(size_file_info + size_message + 1), '.');

  std::snprintf(s.data(), static_cast<size_t>(size_file_info) + 1,
                fmt_file_info, file_name, file_line);

  va_start(args, fmt);
  std::vsnprintf(s.data() + size_file_info,
                 static_cast<size_t>(size_message) + 1, fmt, args);
  va_end(args);

  s.pop_back();  // null-terminating char

  stacktrace.push_back(std::move(s));
}

void LogStacktraceMessage(Stacktrace& stacktrace,
                          const char* file_name,
                          int file_line) {
  const char* const fmt = "%s:%d <no message>";
  auto size_message = std::snprintf(nullptr, 0, fmt, file_name, file_line);
  auto s = std::string(static_cast<size_t>(size_message) + 1, '.');
  std::snprintf(s.data(), static_cast<size_t>(size_message) + 1, fmt, file_name,
                file_line);

  s.pop_back();  // null-terminating char

  stacktrace.push_back(std::move(s));
}

}  // namespace protovm
}  // namespace perfetto
