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

#include <cinttypes>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/string_splitter.h"
#include "perfetto/ext/base/string_utils.h"

#include "perfetto/ext/tracing/core/trace_writer.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

using perfetto::protos::pbzero::AndroidUserListConfig;

namespace perfetto {

// static
const ProbesDataSource::Descriptor UserListDataSource::descriptor = {
    /*type*/ "android.user_list",
    /*flags*/ Descriptor::kFlagsNone,
    /*fill_descriptor_func*/ nullptr,
};

UserListDataSource::UserListDataSource(const DataSourceConfig& ds_config,
                                       TracingSessionID session_id,
                                       std::unique_ptr<TraceWriter> writer)
    : ProbesDataSource(session_id, &descriptor), writer_(std::move(writer)) {
  AndroidUserListConfig::Decoder cfg(ds_config.user_list_config_raw());

  for (auto type = cfg.user_type_filter(); type; ++type) {
    user_type_filter_.emplace((*type).ToStdString());
  }
}

void UserListDataSource::Start() {
  auto trace_packet = writer_->NewTracePacket();
  auto* user_list_packet = trace_packet->set_user_list();
  int error_code = 0;

  base::ScopedFstream fs(fopen("/data/system/users/user.list", "r"));
  if (!fs) {
    error_code = errno;  // Capture errno from fopen failure
    PERFETTO_ELOG("Failed to open user.list: %s", strerror(error_code));
    user_list_packet->set_error(error_code);
    trace_packet->Finalize();
    writer_->Flush();
    return;
  }

  error_code = ParseUserListStream(user_list_packet, fs, user_type_filter_);
  if (error_code != 0) {
    PERFETTO_ELOG("Failed to parse user.list content: %s",
                  strerror(error_code));
    user_list_packet->set_error(error_code);
  }

  if (ferror(*fs)) {
    error_code = errno;  // Capture errno from stream error
    PERFETTO_ELOG("Error reading user.list: %s", strerror(error_code));
    // Overwrite any previous error, as read error is more fundamental.
    user_list_packet->set_error(error_code);
  }

  trace_packet->Finalize();
  writer_->Flush();
}

void UserListDataSource::Flush(FlushRequestID, std::function<void()> callback) {
  // Flush is no-op. We flush after the first write.
  callback();
}

UserListDataSource::~UserListDataSource() = default;

// parser

// Returns 0 on success, EPROTO on parsing failure.
int ReadUserListLine(char* line, User* user) {
  line[strcspn(line, "\n")] = 0;
  size_t idx = 0;
  for (base::StringSplitter str_splitter(line, ' '); str_splitter.Next();) {
    switch (idx) {
      case 0:
        user->type = std::string(str_splitter.cur_token(),
                                 str_splitter.cur_token_size());
        break;
      case 1: {
        std::optional<int32_t> cur_uid =
            base::CStringToInt32(str_splitter.cur_token());
        if (cur_uid.has_value()) {
          user->uid = cur_uid.value();
        } else {
          PERFETTO_ELOG("Failed to parse user.list cur_uid.");
          return -1;  // Protocol error
        }
        break;
      }
    }
    ++idx;
  }
  if (idx < 2) {
    PERFETTO_ELOG("Incomplete line in user.list.");
    return -1;
  }
  return 0;  // Success
}

// Definition of the function declared in user_list_data_source.h
// Returns 0 on success, EPROTO if any line fails to parse.
int ParseUserListStream(protos::pbzero::AndroidUserList* user_list_packet,
                        const base::ScopedFstream& fs,
                        const std::set<std::string>& user_type_filter) {
  char line[2048];
  const std::string filtered_type = "android.os.usertype.FILTERED";

  while (fgets(line, sizeof(line), *fs) != nullptr) {
    User usr_struct;
    if (ReadUserListLine(line, &usr_struct) < 0) {
      return -1;  // Return on first line parse error
    }

    auto* user = user_list_packet->add_users();

    // Check if the filter is active and if the type is NOT in the filter.
    if (!user_type_filter.empty() &&
        user_type_filter.count(usr_struct.type) == 0) {
      // Type is not in the filter, set to android.os.usertype.FILTERED.
      user->set_type(filtered_type.c_str(), filtered_type.size());
    } else {
      // Type is in the filter or the filter is empty, use the original type.
      user->set_type(usr_struct.type.c_str(), usr_struct.type.size());
    }
    user->set_uid(usr_struct.uid);
  }
  return 0;  // Success
}

}  // namespace perfetto
