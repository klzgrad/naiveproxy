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

#ifndef SRC_TRACED_PROBES_JOURNALD_JOURNALD_DATA_SOURCE_H_
#define SRC_TRACED_PROBES_JOURNALD_JOURNALD_DATA_SOURCE_H_

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "perfetto/ext/base/weak_ptr.h"
#include "perfetto/tracing/core/forward_decls.h"
#include "src/traced/probes/probes_data_source.h"

// Forward-declare the C struct to avoid pulling in <systemd/sd-journal.h>
// into every TU that includes this header.
struct sd_journal;

namespace perfetto {

class TraceWriter;
namespace base {
class TaskRunner;
}

class JournaldDataSource : public ProbesDataSource {
 public:
  static const ProbesDataSource::Descriptor descriptor;

  struct Stats {
    uint64_t num_total = 0;
    uint64_t num_failed = 0;
  };

  JournaldDataSource(DataSourceConfig ds_config,
                     base::TaskRunner* task_runner,
                     TracingSessionID session_id,
                     std::unique_ptr<TraceWriter> writer);

  ~JournaldDataSource() override;

  // ProbesDataSource implementation.
  void Start() override;
  void Flush(FlushRequestID, std::function<void()> callback) override;

  const Stats& stats() const { return stats_; }

 private:
  void OnJournalReadable();
  void ReadJournalEntries();

  base::TaskRunner* const task_runner_;
  std::unique_ptr<TraceWriter> writer_;
  sd_journal* journal_ = nullptr;
  int fd_ = -1;

  struct SdJournalApi;
  std::unique_ptr<SdJournalApi> sd_;
  std::unique_ptr<SdJournalApi> LoadSdJournalApi();

  // Config parameters.
  uint32_t min_prio_ = 7;  // 7=DEBUG, capture everything by default.
  std::vector<std::string> filter_identifiers_;
  std::vector<std::string> filter_units_;

  Stats stats_;
  base::WeakPtrFactory<JournaldDataSource> weak_factory_;  // Keep last.
};

}  // namespace perfetto

#endif  // SRC_TRACED_PROBES_JOURNALD_JOURNALD_DATA_SOURCE_H_
