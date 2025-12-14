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

#include "perfetto/base/task_runner.h"
#include "perfetto/base/time.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/string_splitter.h"

#include "perfetto/ext/tracing/core/trace_writer.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

#include "src/traced/probes/common/android_cpu_per_uid_poller.h"
#include "src/traced/probes/packages_list/packages_list_parser.h"

using perfetto::protos::pbzero::PackagesListConfig;

namespace perfetto {

namespace {
constexpr uint32_t kFirstPackageUid = 10000;
constexpr uint32_t kMinPollIntervalMs = 60000;
constexpr uint32_t kWriteAllAtStart = 0xffffffff;
}  // namespace

// static
const ProbesDataSource::Descriptor PackagesListDataSource::descriptor = {
    /*name*/ "android.packages_list",
    /*flags*/ Descriptor::kFlagsNone,
    /*fill_descriptor_func*/ nullptr,
};

bool ParsePackagesListStream(
    std::unordered_multimap<uint64_t, Package>& packages,
    const base::ScopedFstream& fs,
    const std::set<std::string>& package_name_filter) {
  bool parse_error = false;
  char line[2048];
  while (fgets(line, sizeof(line), *fs) != nullptr) {
    Package pkg_struct;
    if (!ReadPackagesListLine(line, &pkg_struct)) {
      parse_error = true;
      continue;
    }
    if (!package_name_filter.empty() &&
        package_name_filter.count(pkg_struct.name) == 0) {
      continue;
    }
    packages.insert({pkg_struct.uid, pkg_struct});
  }
  return parse_error;
}

PackagesListDataSource::PackagesListDataSource(
    const DataSourceConfig& ds_config,
    base::TaskRunner* task_runner,
    TracingSessionID session_id,
    std::unique_ptr<TraceWriter> writer)
    : ProbesDataSource(session_id, &descriptor),
      task_runner_(task_runner),
      poller_(std::make_unique<AndroidCpuPerUidPoller>()),
      writer_(std::move(writer)),
      weak_factory_(this) {
  PackagesListConfig::Decoder cfg(ds_config.packages_list_config_raw());
  for (auto name = cfg.package_name_filter(); name; ++name) {
    package_name_filter_.emplace((*name).ToStdString());
  }

  only_write_on_cpu_use_every_ms_ = cfg.only_write_on_cpu_use_every_ms();
  if (only_write_on_cpu_use_every_ms_ != 0) {
    if (only_write_on_cpu_use_every_ms_ < kMinPollIntervalMs) {
      PERFETTO_ELOG("Package list on-use poll interval of %" PRIu32
                    " ms is too low. Capping to %" PRIu32 " ms",
                    only_write_on_cpu_use_every_ms_, kMinPollIntervalMs);
      only_write_on_cpu_use_every_ms_ = kMinPollIntervalMs;
    }

  } else {
    only_write_on_cpu_use_every_ms_ = kWriteAllAtStart;
  }
}

void PackagesListDataSource::Start() {
  base::ScopedFstream fs(fopen("/data/system/packages.list", "r"));
  if (fs) {
    packages_parse_error_ =
        ParsePackagesListStream(packages_, fs, package_name_filter_);
    if (ferror(*fs))
      packages_read_error_ = true;

  } else {
    PERFETTO_ELOG("Failed to open packages.list");
    packages_read_error_ = true;
  }

  if (only_write_on_cpu_use_every_ms_ == kWriteAllAtStart) {
    auto trace_packet = writer_->NewTracePacket();
    auto* packages_list_packet = trace_packet->set_packages_list();
    if (packages_parse_error_)
      packages_list_packet->set_parse_error(true);

    if (packages_read_error_)
      packages_list_packet->set_read_error(true);

    for (auto it = packages_.begin(); it != packages_.end(); ++it) {
      auto package = it->second;
      auto* package_proto = packages_list_packet->add_packages();
      package_proto->set_name(package.name.c_str(), package.name.size());
      package_proto->set_uid(package.uid);
      package_proto->set_debuggable(package.debuggable);
      package_proto->set_profileable_from_shell(package.profileable_from_shell);
      package_proto->set_version_code(package.version_code);
    }

    trace_packet->Finalize();
    writer_->Flush();
  } else {
    poller_->Start();
    Tick();
  }
}

void PackagesListDataSource::Tick() {
  // Post next task.
  auto now_ms = base::GetWallTimeMs().count();
  auto weak_this = weak_factory_.GetWeakPtr();
  task_runner_->PostDelayedTask(
      [weak_this] {
        if (weak_this)
          weak_this->Tick();
      },
      only_write_on_cpu_use_every_ms_ -
          static_cast<uint32_t>(now_ms % only_write_on_cpu_use_every_ms_));

  WriteIncrementalPacket();
}

void PackagesListDataSource::WriteIncrementalPacket() {
  std::vector<CpuPerUidTime> cpu_times = poller_->Poll();

  if (first_time_) {
    // The first time we poll the poller it will return details of all UIDs,
    // even ones whose last activity was days ago, so we wait until a subsequent
    // time for evidence something has actually run.
    first_time_ = false;
    return;
  }

  std::vector<uint32_t> new_uids;
  for (auto& time : cpu_times) {
    if (seen_uids_.insert(time.uid).second) {
      new_uids.push_back(time.uid);
    }
  }

  if (!new_uids.empty()) {
    auto trace_packet = writer_->NewTracePacket();
    auto* packages_list_packet = trace_packet->set_packages_list();
    if (packages_parse_error_)
      packages_list_packet->set_parse_error(true);

    if (packages_read_error_)
      packages_list_packet->set_read_error(true);

    for (auto uid : new_uids) {
      auto range = packages_.equal_range(uid);
      if (range.first == range.second) {
        if (uid >= kFirstPackageUid) {
          PERFETTO_ELOG("No package in list for uid %u", uid);
        }
        continue;
      }
      for (auto it = range.first; it != range.second; ++it) {
        auto& package = it->second;
        auto* package_proto = packages_list_packet->add_packages();
        package_proto->set_name(package.name.c_str(), package.name.size());
        package_proto->set_uid(package.uid);
        package_proto->set_debuggable(package.debuggable);
        package_proto->set_profileable_from_shell(
            package.profileable_from_shell);
        package_proto->set_version_code(package.version_code);
      }
    }
  }
}

void PackagesListDataSource::Flush(FlushRequestID,
                                   std::function<void()> callback) {
  if (only_write_on_cpu_use_every_ms_ == kWriteAllAtStart) {
    // Flush is no-op. We flush after the first write.
    callback();
  } else {
    writer_->Flush(callback);
    poller_->Clear();
  }
}

void PackagesListDataSource::ClearIncrementalState() {
  seen_uids_.clear();
}

PackagesListDataSource::~PackagesListDataSource() = default;

}  // namespace perfetto
