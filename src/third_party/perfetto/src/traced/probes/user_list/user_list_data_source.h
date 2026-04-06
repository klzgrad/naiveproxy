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

#ifndef SRC_TRACED_PROBES_USER_LIST_USER_LIST_DATA_SOURCE_H_
#define SRC_TRACED_PROBES_USER_LIST_USER_LIST_DATA_SOURCE_H_

#include <cinttypes>
#include <functional>
#include <memory>
#include <set>
#include <string>

#include "perfetto/base/task_runner.h"
#include "perfetto/ext/base/scoped_file.h"

#include "perfetto/ext/tracing/core/basic_types.h"
#include "perfetto/tracing/core/data_source_config.h"
#include "protos/perfetto/config/android/user_list_config.pbzero.h"
#include "protos/perfetto/trace/android/user_list.pbzero.h"

#include "src/traced/probes/probes_data_source.h"

namespace perfetto {

class TraceWriter;

struct User {
  std::string type;
  int32_t uid = 0;
};

int ParseUserListStream(protos::pbzero::AndroidUserList* user_list,
                        const base::ScopedFstream& fs,
                        const std::set<std::string>& user_type_filter);

int ReadUserListLine(char* line, User* user);

class UserListDataSource : public ProbesDataSource {
 public:
  static const ProbesDataSource::Descriptor descriptor;

  UserListDataSource(const DataSourceConfig& ds_config,
                     TracingSessionID session_id,
                     std::unique_ptr<TraceWriter> writer);
  // ProbesDataSource implementation.
  void Start() override;
  void Flush(FlushRequestID, std::function<void()> callback) override;

  ~UserListDataSource() override;

 private:
  // If empty, include all user types. std::set over std::unordered_set as
  // this should be trivially small (or empty) in practice, and the latter uses
  // ever so slightly more memory.
  std::set<std::string> user_type_filter_;
  std::unique_ptr<TraceWriter> writer_;
};

}  // namespace perfetto

#endif  // SRC_TRACED_PROBES_USER_LIST_USER_LIST_DATA_SOURCE_H_
