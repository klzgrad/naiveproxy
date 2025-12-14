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

#ifndef SRC_TRACED_PROBES_PACKAGES_LIST_PACKAGES_LIST_DATA_SOURCE_H_
#define SRC_TRACED_PROBES_PACKAGES_LIST_PACKAGES_LIST_DATA_SOURCE_H_

#include <functional>
#include <memory>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "perfetto/base/task_runner.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/weak_ptr.h"

#include "perfetto/ext/tracing/core/basic_types.h"
#include "perfetto/tracing/core/data_source_config.h"
#include "protos/perfetto/config/android/packages_list_config.pbzero.h"
#include "protos/perfetto/trace/android/packages_list.pbzero.h"

#include "src/traced/probes/packages_list/packages_list_parser.h"
#include "src/traced/probes/probes_data_source.h"

namespace perfetto {

class TraceWriter;
class AndroidCpuPerUidPoller;

bool ParsePackagesListStream(
    std::unordered_multimap<uint64_t, Package>& packages,
    const base::ScopedFstream& fs,
    const std::set<std::string>& package_name_filter);

class PackagesListDataSource : public ProbesDataSource {
 public:
  static const ProbesDataSource::Descriptor descriptor;

  PackagesListDataSource(const DataSourceConfig& ds_config,
                         base::TaskRunner* task_runner,
                         TracingSessionID session_id,
                         std::unique_ptr<TraceWriter> writer);
  // ProbesDataSource implementation.
  void Start() override;
  void Flush(FlushRequestID, std::function<void()> callback) override;
  void ClearIncrementalState() override;

  ~PackagesListDataSource() override;

 private:
  void Tick();
  void WriteIncrementalPacket();

  base::WeakPtr<PackagesListDataSource> GetWeakPtr() const;

  // Used in polling mode.
  uint32_t only_write_on_cpu_use_every_ms_;
  std::unordered_set<uint32_t> seen_uids_;
  base::TaskRunner* const task_runner_;
  std::unique_ptr<AndroidCpuPerUidPoller> poller_;
  bool first_time_ = true;

  std::unordered_multimap<uint64_t, Package> packages_;
  bool packages_parse_error_;
  bool packages_read_error_;

  // If empty, include all package names. std::set over std::unordered_set as
  // this should be trivially small (or empty) in practice, and the latter uses
  // ever so slightly more memory.
  std::set<std::string> package_name_filter_;
  std::unique_ptr<TraceWriter> writer_;
  base::WeakPtrFactory<PackagesListDataSource> weak_factory_;  // Keep last.
};

}  // namespace perfetto

#endif  // SRC_TRACED_PROBES_PACKAGES_LIST_PACKAGES_LIST_DATA_SOURCE_H_
