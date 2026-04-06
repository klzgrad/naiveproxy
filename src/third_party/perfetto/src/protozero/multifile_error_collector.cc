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

#include "src/protozero/multifile_error_collector.h"

#include "perfetto/base/logging.h"

namespace protozero {

MultiFileErrorCollectorImpl::~MultiFileErrorCollectorImpl() = default;

#if GOOGLE_PROTOBUF_VERSION >= 4022000
void MultiFileErrorCollectorImpl::RecordError(std::string_view filename,
                                              int line,
                                              int column,
                                              std::string_view message) {
  PERFETTO_ELOG("Error %.*s %d:%d: %.*s", static_cast<int>(filename.size()),
                filename.data(), line, column, static_cast<int>(message.size()),
                message.data());
}

void MultiFileErrorCollectorImpl::RecordWarning(std::string_view filename,
                                                int line,
                                                int column,
                                                std::string_view message) {
  PERFETTO_ELOG("Warning %.*s %d:%d: %.*s", static_cast<int>(filename.size()),
                filename.data(), line, column, static_cast<int>(message.size()),
                message.data());
}
#else
void MultiFileErrorCollectorImpl::AddError(const std::string& filename,
                                           int line,
                                           int column,
                                           const std::string& message) {
  PERFETTO_ELOG("Error %s %d:%d: %s", filename.c_str(), line, column,
                message.c_str());
}

void MultiFileErrorCollectorImpl::AddWarning(const std::string& filename,
                                             int line,
                                             int column,
                                             const std::string& message) {
  PERFETTO_ELOG("Warning %s %d:%d: %s", filename.c_str(), line, column,
                message.c_str());
}
#endif

}  // namespace protozero
