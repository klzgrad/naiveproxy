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

#ifndef SRC_TRACED_PROBES_ANDROID_CPU_PER_UID_ANDROID_CPU_PER_UID_DATA_SOURCE_H_
#define SRC_TRACED_PROBES_ANDROID_CPU_PER_UID_ANDROID_CPU_PER_UID_DATA_SOURCE_H_

#include <memory>

#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/weak_ptr.h"
#include "perfetto/tracing/core/data_source_config.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"
#include "src/traced/probes/probes_data_source.h"

namespace perfetto {

class TraceWriter;
class AndroidCpuPerUidPoller;
namespace base {
class TaskRunner;
}

class AndroidCpuPerUidDataSource : public ProbesDataSource {
 public:
  static const ProbesDataSource::Descriptor descriptor;

  AndroidCpuPerUidDataSource(const DataSourceConfig&,
                             base::TaskRunner*,
                             TracingSessionID,
                             std::unique_ptr<TraceWriter> writer);

  ~AndroidCpuPerUidDataSource() override;

  // ProbesDataSource implementation.
  void Start() override;
  void Flush(FlushRequestID, std::function<void()> callback) override;
  void ClearIncrementalState() override;

 private:
  void Tick();
  void WriteCpuPerUid();

  base::WeakPtr<AndroidCpuPerUidDataSource> GetWeakPtr() const;

  uint32_t poll_interval_ms_ = 0;
  bool first_time_ = true;

  base::TaskRunner* const task_runner_;
  std::unique_ptr<TraceWriter> writer_;
  std::unique_ptr<AndroidCpuPerUidPoller> poller_;
  base::WeakPtrFactory<AndroidCpuPerUidDataSource> weak_factory_;  // Keep last.
};

}  // namespace perfetto

#endif  // SRC_TRACED_PROBES_ANDROID_CPU_PER_UID_ANDROID_CPU_PER_UID_DATA_SOURCE_H_
