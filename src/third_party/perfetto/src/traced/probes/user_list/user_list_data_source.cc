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

#include "src/traced/probes/user_list/user_list_data_source.h"

#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/string_splitter.h"

#include "perfetto/ext/tracing/core/trace_writer.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

#include "src/traced/probes/user_list/user_list_parser.h"

using perfetto::protos::pbzero::UserListConfig;

namespace perfetto {

// static
const ProbesDataSource::Descriptor UserListDataSource::descriptor = {
    /*type*/ "android.user_list",
    /*flags*/ Descriptor::kFlagsNone,
    /*fill_descriptor_func*/ nullptr,
};

bool ParseUserListStream(protos::pbzero::UserList* user_list_packet,
                         const base::ScopedFstream& fs,
                         const std::set<std::string>& user_type_filter) {
  bool parsed_fully = true;
  char line[2048];
  while (fgets(line, sizeof(line), *fs) != nullptr) {
    User pkg_struct;
    if (!ReadUserListLine(line, &pkg_struct)) {
      parsed_fully = false;
      continue;
    }
    if (!user_type_filter.empty() &&
        user_type_filter.count(pkg_struct.type) == 0) {
      continue;
    }
    auto* user = user_list_packet->add_users();
    user->set_type(pkg_struct.type.c_str(), pkg_struct.type.size());
    user->set_uid(pkg_struct.uid);
  }
  return parsed_fully;
}

UserListDataSource::UserListDataSource(const DataSourceConfig& ds_config,
                                       TracingSessionID session_id,
                                       std::unique_ptr<TraceWriter> writer)
    : ProbesDataSource(session_id, &descriptor), writer_(std::move(writer)) {
  UserListConfig::Decoder cfg(ds_config.user_list_config_raw());
  for (auto type = cfg.user_type_filter(); type; ++type) {
    user_type_filter_.emplace((*type).ToStdString());
  }
}

void UserListDataSource::Start() {
  base::ScopedFstream fs(fopen("/data/system/users/user.list", "r"));
  auto trace_packet = writer_->NewTracePacket();
  auto* user_list_packet = trace_packet->set_user_list();
  if (!fs) {
    PERFETTO_ELOG("Failed to open user.list");
    user_list_packet->set_read_error(true);
    trace_packet->Finalize();
    writer_->Flush();
    return;
  }

  bool parsed_fully =
      ParseUserListStream(user_list_packet, fs, user_type_filter_);
  if (!parsed_fully)
    user_list_packet->set_parse_error(true);

  if (ferror(*fs))
    user_list_packet->set_read_error(true);

  trace_packet->Finalize();
  writer_->Flush();
}

void UserListDataSource::Flush(FlushRequestID, std::function<void()> callback) {
  // Flush is no-op. We flush after the first write.
  callback();
}

UserListDataSource::~UserListDataSource() = default;

}  // namespace perfetto
