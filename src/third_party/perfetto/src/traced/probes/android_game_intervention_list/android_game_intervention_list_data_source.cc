/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "src/traced/probes/android_game_intervention_list/android_game_intervention_list_data_source.h"

#include <stddef.h>
#include <optional>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/string_splitter.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/tracing/core/trace_writer.h"

#include "perfetto/tracing/core/data_source_config.h"
#include "protos/perfetto/config/android/android_game_intervention_list_config.pbzero.h"
#include "protos/perfetto/trace/android/android_game_intervention_list.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto {

const char kAndroidGameInterventionListFileName[] =
    "/data/system/game_mode_intervention.list";

// making the descriptor static
const ProbesDataSource::Descriptor
    AndroidGameInterventionListDataSource::descriptor = {
        /* name */ "android.game_interventions",
        /* flags */ Descriptor::kFlagsNone,
        /* fill_descriptor_func */ nullptr,
};

AndroidGameInterventionListDataSource::AndroidGameInterventionListDataSource(
    const DataSourceConfig& ds_config,
    TracingSessionID session_id,
    std::unique_ptr<TraceWriter> trace_writer)
    : ProbesDataSource(session_id, &descriptor),
      trace_writer_(std::move(trace_writer)) {
  perfetto::protos::pbzero::AndroidGameInterventionListConfig::Decoder cfg(
      ds_config.android_game_intervention_list_config_raw());
  for (auto name = cfg.package_name_filter(); name; ++name) {
    package_name_filter_.emplace_back((*name).ToStdString());
  }
}

AndroidGameInterventionListDataSource::
    ~AndroidGameInterventionListDataSource() = default;

void AndroidGameInterventionListDataSource::Start() {
  auto trace_packet = trace_writer_->NewTracePacket();
  auto* android_game_intervention_list_packet =
      trace_packet->set_android_game_intervention_list();

  base::ScopedFstream fs(fopen(kAndroidGameInterventionListFileName, "r"));
  if (!fs) {
    PERFETTO_ELOG("Failed to open %s", kAndroidGameInterventionListFileName);
    android_game_intervention_list_packet->set_read_error(true);
  } else {
    bool is_parsed_fully = ParseAndroidGameInterventionListStream(
        android_game_intervention_list_packet, fs, package_name_filter_);
    if (!is_parsed_fully) {
      android_game_intervention_list_packet->set_parse_error(true);
    }
    if (ferror(*fs)) {
      android_game_intervention_list_packet->set_read_error(true);
    }
  }

  trace_packet->Finalize();
  trace_writer_->Flush();
}

void AndroidGameInterventionListDataSource::Flush(
    FlushRequestID,
    std::function<void()> callback) {
  callback();
}

bool AndroidGameInterventionListDataSource::
    ParseAndroidGameInterventionListStream(
        protos::pbzero::AndroidGameInterventionList* packet,
        const base::ScopedFstream& fs,
        const std::vector<std::string>& package_name_filter) {
  bool is_parsed_fully = true;
  char line_buf[2048];
  while (fgets(line_buf, sizeof(line_buf), *fs) != nullptr) {
    // removing trailing '\n'
    // for processing fields with CStringTo* functions
    line_buf[strlen(line_buf) - 1] = '\0';

    if (!ParseAndroidGameInterventionListLine(line_buf, package_name_filter,
                                              packet)) {
      // marking parsed with error and continue with this line skipped
      is_parsed_fully = false;
    }
  }
  return is_parsed_fully;
}

bool AndroidGameInterventionListDataSource::
    ParseAndroidGameInterventionListLine(
        char* line,
        const std::vector<std::string>& package_name_filter,
        protos::pbzero::AndroidGameInterventionList* packet) {
  size_t idx = 0;
  perfetto::protos::pbzero::AndroidGameInterventionList_GamePackageInfo*
      package = nullptr;
  perfetto::protos::pbzero::AndroidGameInterventionList_GameModeInfo*
      game_mode_info = nullptr;
  for (base::StringSplitter string_splitter(line, '\t'); string_splitter.Next();
       ++idx) {
    // check if package name is in the name filter
    // if not we skip parsing this line.
    if (idx == 0) {
      if (!package_name_filter.empty() &&
          std::count(package_name_filter.begin(), package_name_filter.end(),
                     string_splitter.cur_token()) == 0) {
        return true;
      }
      package = packet->add_game_packages();
    }

    switch (idx) {
      case 0: {
        package->set_name(string_splitter.cur_token(),
                          string_splitter.cur_token_size());
        break;
      }
      case 1: {
        std::optional<uint64_t> uid =
            base::CStringToUInt64(string_splitter.cur_token());
        if (uid == std::nullopt) {
          PERFETTO_DLOG("Failed to parse game_mode_intervention.list uid.");
          return false;
        }
        package->set_uid(uid.value());
        break;
      }
      case 2: {
        std::optional<uint32_t> cur_mode =
            base::CStringToUInt32(string_splitter.cur_token());
        if (cur_mode == std::nullopt) {
          PERFETTO_DLOG(
              "Failed to parse game_mode_intervention.list cur_mode.");
          return false;
        }
        package->set_current_mode(cur_mode.value());
        break;
      }
      case 3:
      case 5:
      case 7: {
        std::optional<uint32_t> game_mode =
            base::CStringToUInt32(string_splitter.cur_token());
        if (game_mode == std::nullopt) {
          PERFETTO_DLOG(
              "Failed to parse game_mode_intervention.list game_mode.");
          return false;
        }
        game_mode_info = package->add_game_mode_info();
        game_mode_info->set_mode(game_mode.value());
        break;
      }
      case 4:
      case 6:
      case 8: {
        for (base::StringSplitter intervention_splitter(
                 string_splitter.cur_token(), ',');
             intervention_splitter.Next();) {
          base::StringSplitter value_splitter(intervention_splitter.cur_token(),
                                              '=');
          value_splitter.Next();
          char* key = value_splitter.cur_token();
          if (strcmp(key, "angle") == 0) {
            value_splitter.Next();
            std::optional<uint32_t> use_angle =
                base::CStringToUInt32(value_splitter.cur_token());
            if (use_angle == std::nullopt) {
              PERFETTO_DLOG(
                  "Failed to parse game_mode_intervention.list use_angle.");
              return false;
            }
            game_mode_info->set_use_angle(use_angle.value());
          } else if (strcmp(key, "scaling") == 0) {
            value_splitter.Next();
            std::optional<double> resolution_downscale =
                base::CStringToDouble(value_splitter.cur_token());
            if (resolution_downscale == std::nullopt) {
              PERFETTO_DLOG(
                  "Failed to parse game_mode_intervention.list "
                  "resolution_downscale.");
              return false;
            }
            game_mode_info->set_resolution_downscale(
                static_cast<float>(resolution_downscale.value()));
          } else if (strcmp(key, "fps") == 0) {
            value_splitter.Next();
            std::optional<double> fps =
                base::CStringToDouble(value_splitter.cur_token());
            if (fps == std::nullopt) {
              PERFETTO_DLOG("Failed to parse game_mode_intervention.list fps.");
              return false;
            }
            game_mode_info->set_fps(static_cast<float>(fps.value()));
          }
        }
        break;
      }
    }
  }
  return true;
}

}  // namespace perfetto
