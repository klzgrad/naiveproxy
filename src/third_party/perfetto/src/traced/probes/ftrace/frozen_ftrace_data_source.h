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

#ifndef SRC_TRACED_PROBES_FTRACE_FROZEN_FTRACE_DATA_SOURCE_H_
#define SRC_TRACED_PROBES_FTRACE_FROZEN_FTRACE_DATA_SOURCE_H_

#include <functional>
#include <memory>

#include "perfetto/ext/base/weak_ptr.h"
#include "perfetto/ext/tracing/core/basic_types.h"
#include "perfetto/ext/tracing/core/trace_writer.h"
#include "perfetto/tracing/core/forward_decls.h"
#include "src/traced/probes/ftrace/cpu_reader.h"
#include "src/traced/probes/ftrace/tracefs.h"
#include "src/traced/probes/probes_data_source.h"

#include "protos/perfetto/config/ftrace/frozen_ftrace_config.gen.h"

namespace perfetto {
struct FtraceDataSourceConfig;
class ProtoTranslationTable;

namespace base {
class TaskRunner;
}

// Consumes the contents of a stopped tracefs instance, converting them to
// perfetto ftrace protos (same as FtraceDataSource). Does not reactivate the
// instance or write to any other control files within the tracefs instance (but
// the buffer contents do get consumed).
class FrozenFtraceDataSource : public ProbesDataSource {
 public:
  static const ProbesDataSource::Descriptor descriptor;

  FrozenFtraceDataSource(base::TaskRunner* task_runner,
                         const DataSourceConfig& ds_config,
                         TracingSessionID session_id,
                         std::unique_ptr<TraceWriter> writer);
  ~FrozenFtraceDataSource() override;

  // ProbeDataSource implementation.
  void Start() override;
  void Flush(FlushRequestID, std::function<void()> callback) override;

  base::WeakPtr<FrozenFtraceDataSource> GetWeakPtr() const {
    return weak_factory_.GetWeakPtr();
  }

 private:
  void ReadTask();

  // This is the maximum number of pages reading at once from
  // per-cpu buffer. To prevent blocking other services, keep
  // it small enough.
  static constexpr size_t kFrozenFtraceMaxReadPages = 32;

  base::TaskRunner* const task_runner_;
  std::unique_ptr<TraceWriter> writer_;

  protos::gen::FrozenFtraceConfig ds_config_;

  std::unique_ptr<Tracefs> tracefs_;
  std::unique_ptr<ProtoTranslationTable> translation_table_;
  std::unique_ptr<FtraceDataSourceConfig> parsing_config_;
  CpuReader::ParsingBuffers parsing_mem_;
  std::vector<CpuReader> cpu_readers_;

  std::vector<size_t> cpu_page_quota_;
  // Storing parsed metadata (e.g. pid)
  FtraceMetadata metadata_;

  base::FlatSet<protos::pbzero::FtraceParseStatus> parse_errors_;

  base::WeakPtrFactory<FrozenFtraceDataSource> weak_factory_;  // Keep last.
};

}  // namespace perfetto

#endif  // SRC_TRACED_PROBES_FTRACE_FROZEN_FTRACE_DATA_SOURCE_H_
