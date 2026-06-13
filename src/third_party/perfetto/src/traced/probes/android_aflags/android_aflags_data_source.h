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

#ifndef SRC_TRACED_PROBES_ANDROID_AFLAGS_ANDROID_AFLAGS_DATA_SOURCE_H_
#define SRC_TRACED_PROBES_ANDROID_AFLAGS_ANDROID_AFLAGS_DATA_SOURCE_H_

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "perfetto/ext/base/subprocess.h"
#include "perfetto/ext/base/weak_ptr.h"
#include "perfetto/ext/tracing/core/trace_writer.h"
#include "src/traced/probes/probes_data_source.h"

namespace perfetto {

namespace base {
class TaskRunner;
}  // namespace base

class AndroidAflagsDataSource : public ProbesDataSource {
 public:
  static const ProbesDataSource::Descriptor descriptor;

  AndroidAflagsDataSource(const DataSourceConfig& ds_config,
                          base::TaskRunner* task_runner,
                          TracingSessionID session_id,
                          std::unique_ptr<TraceWriter> writer);
  AndroidAflagsDataSource(const AndroidAflagsDataSource&) = delete;
  AndroidAflagsDataSource& operator=(const AndroidAflagsDataSource&) = delete;
  ~AndroidAflagsDataSource() override;

  void Start() override;
  void Flush(FlushRequestID, std::function<void()> callback) override;

 protected:
  // Finalizes the capture of aflags data. Decodes the collected
  // base64-encoded output and emits it to the trace.
  void FinalizeAflagsCapture(std::string error);

  // Non-blocking pipe to asynchronously read the output of `aflags list`.
  // This must be declared before aflags_process_ to ensure the pipe outlives
  // the subprocess.
  base::ScopedPlatformHandle aflags_output_pipe_;

  // The running `aflags list` process.
  std::unique_ptr<base::Subprocess> aflags_process_;

  // Buffer used to accumulate the base64-encoded output of `aflags list`.
  std::string aflags_output_;

 private:
  void Tick();

  // Invoked when the `aflags list` process has output to read.
  void OnAflagsOutput();

  // Emits a trace packet with AndroidAflags.error set to |error_msg|.
  void EmitErrorPacket(const std::string& error_msg);

  base::TaskRunner* const task_runner_;
  std::unique_ptr<TraceWriter> writer_;

  // Polling frequency in ms. 0 means one-shot capture at startup.
  uint32_t poll_period_ms_ = 0;

  // Flush() callbacks deferred while an aflags subprocess is in flight.
  // Drained when the subprocess completes (or when the flush timeout fires).
  std::vector<std::function<void()>> pending_flushes_;

  base::WeakPtrFactory<AndroidAflagsDataSource> weak_factory_;
};

}  // namespace perfetto

#endif  // SRC_TRACED_PROBES_ANDROID_AFLAGS_ANDROID_AFLAGS_DATA_SOURCE_H_
