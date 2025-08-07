/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "src/traced/probes/packages_list/packages_list_data_source.h"

#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/string_splitter.h"

#include "perfetto/ext/tracing/core/trace_writer.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

#include "src/traced/probes/packages_list/packages_list_parser.h"

using perfetto::protos::pbzero::PackagesListConfig;

namespace perfetto {

// static
const ProbesDataSource::Descriptor PackagesListDataSource::descriptor = {
    /*name*/ "android.packages_list",
    /*flags*/ Descriptor::kFlagsNone,
    /*fill_descriptor_func*/ nullptr,
};

bool ParsePackagesListStream(protos::pbzero::PackagesList* packages_list_packet,
                             const base::ScopedFstream& fs,
                             const std::set<std::string>& package_name_filter) {
  bool parsed_fully = true;
  char line[2048];
  while (fgets(line, sizeof(line), *fs) != nullptr) {
    Package pkg_struct;
    if (!ReadPackagesListLine(line, &pkg_struct)) {
      parsed_fully = false;
      continue;
    }
    if (!package_name_filter.empty() &&
        package_name_filter.count(pkg_struct.name) == 0) {
      continue;
    }
    auto* package = packages_list_packet->add_packages();
    package->set_name(pkg_struct.name.c_str(), pkg_struct.name.size());
    package->set_uid(pkg_struct.uid);
    package->set_debuggable(pkg_struct.debuggable);
    package->set_profileable_from_shell(pkg_struct.profileable_from_shell);
    package->set_version_code(pkg_struct.version_code);
  }
  return parsed_fully;
}

PackagesListDataSource::PackagesListDataSource(
    const DataSourceConfig& ds_config,
    TracingSessionID session_id,
    std::unique_ptr<TraceWriter> writer)
    : ProbesDataSource(session_id, &descriptor), writer_(std::move(writer)) {
  PackagesListConfig::Decoder cfg(ds_config.packages_list_config_raw());
  for (auto name = cfg.package_name_filter(); name; ++name) {
    package_name_filter_.emplace((*name).ToStdString());
  }
}

void PackagesListDataSource::Start() {
  base::ScopedFstream fs(fopen("/data/system/packages.list", "r"));
  auto trace_packet = writer_->NewTracePacket();
  auto* packages_list_packet = trace_packet->set_packages_list();
  if (!fs) {
    PERFETTO_ELOG("Failed to open packages.list");
    packages_list_packet->set_read_error(true);
    trace_packet->Finalize();
    writer_->Flush();
    return;
  }

  bool parsed_fully =
      ParsePackagesListStream(packages_list_packet, fs, package_name_filter_);
  if (!parsed_fully)
    packages_list_packet->set_parse_error(true);

  if (ferror(*fs))
    packages_list_packet->set_read_error(true);

  trace_packet->Finalize();
  writer_->Flush();
}

void PackagesListDataSource::Flush(FlushRequestID,
                                   std::function<void()> callback) {
  // Flush is no-op. We flush after the first write.
  callback();
}

PackagesListDataSource::~PackagesListDataSource() = default;

}  // namespace perfetto
