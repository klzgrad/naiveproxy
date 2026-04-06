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

#ifndef SRC_PROTOZERO_MULTIFILE_ERROR_COLLECTOR_H_
#define SRC_PROTOZERO_MULTIFILE_ERROR_COLLECTOR_H_

#include <string>
#include <string_view>

#include <google/protobuf/compiler/importer.h>

namespace protozero {

// A simple implementation of protobuf's MultiFileErrorCollector that logs
// errors and warnings using PERFETTO_ELOG.
class MultiFileErrorCollectorImpl
    : public google::protobuf::compiler::MultiFileErrorCollector {
 public:
  ~MultiFileErrorCollectorImpl() override;
#if GOOGLE_PROTOBUF_VERSION >= 4022000
  void RecordError(std::string_view filename,
                   int line,
                   int column,
                   std::string_view message) override;
  void RecordWarning(std::string_view filename,
                     int line,
                     int column,
                     std::string_view message) override;
#else
  void AddError(const std::string& filename,
                int line,
                int column,
                const std::string& message) override;
  void AddWarning(const std::string& filename,
                  int line,
                  int column,
                  const std::string& message) override;
#endif
};

}  // namespace protozero

#endif  // SRC_PROTOZERO_MULTIFILE_ERROR_COLLECTOR_H_
