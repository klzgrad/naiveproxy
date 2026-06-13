/*
 * Copyright (C) 2017 The Android Open Source Project
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

#ifndef SRC_TRACED_PROBES_FTRACE_FTRACE_CONTROLLER_H_
#define SRC_TRACED_PROBES_FTRACE_FTRACE_CONTROLLER_H_

#include <stdint.h>
#include <unistd.h>

#include <map>
#include <memory>
#include <set>
#include <string>

#include "perfetto/base/task_runner.h"
#include "perfetto/ext/base/weak_ptr.h"
#include "perfetto/ext/tracing/core/basic_types.h"
#include "src/kallsyms/lazy_kernel_symbolizer.h"
#include "src/traced/probes/ftrace/atrace_wrapper.h"
#include "src/traced/probes/ftrace/cpu_reader.h"
#include "src/traced/probes/ftrace/ftrace_config_utils.h"

namespace perfetto {

class FtraceConfigMuxer;
class FtraceDataSource;
class Tracefs;
class LazyKernelSymbolizer;
class ProtoTranslationTable;
struct FtraceStats;

// Method of last resort to reset ftrace state.
bool HardResetFtraceState();

// Responsible for controlling the kernel ftrace tracing filesystem (i.e. the
// root tracefs directory at /sys/kernel/tracing/). Records ftrace data as
// possibly-concurrent data sources are started and stopped, overlaying their
// configurations onto a single shared kernel instance.
//
// Makes use of the following notable classes:
// * FtraceConfigMuxer for unioning multiple tracing configs.
// * CpuReader for consuming the kernel ring buffer ftrace data and serialising
//   it as perfetto protobuf tracing packets.
// * ProtoTranslationTable for mapping events from binary to protobuf formats.
class FtraceController {
 public:
  class Observer {
   public:
    virtual ~Observer();
    virtual void OnFtraceDataWrittenIntoDataSourceBuffers() = 0;
  };

  // The passed Observer must outlive the returned FtraceController instance.
  static std::unique_ptr<FtraceController> Create(base::TaskRunner*, Observer*);
  virtual ~FtraceController();

  FtraceController(const FtraceController&) = delete;
  FtraceController& operator=(const FtraceController&) = delete;

  bool AddDataSource(FtraceDataSource*) PERFETTO_WARN_UNUSED_RESULT;
  bool StartDataSource(FtraceDataSource*);
  void RemoveDataSource(FtraceDataSource*);

  // Force a read of the ftrace buffers. Will call OnFtraceFlushComplete() on
  // all started data sources.
  void Flush(FlushRequestID);

  void DumpFtraceStats(FtraceDataSource*, FtraceStats*);

  base::WeakPtr<FtraceController> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

  // public for testing
  static std::optional<std::string> AbsolutePathForInstance(
      const std::string& tracefs_root,
      const std::string& raw_cfg_name);

  // public for testing
  static bool PollSupportedOnKernelVersion(const char* uts_release);

 protected:
  // Everything protected/virtual for testing:

  FtraceController(std::unique_ptr<Tracefs>,
                   std::unique_ptr<ProtoTranslationTable>,
                   std::unique_ptr<AtraceWrapper>,
                   std::unique_ptr<FtraceConfigMuxer>,
                   base::TaskRunner*,
                   Observer*);

  struct FtraceInstanceState {
    FtraceInstanceState(std::unique_ptr<Tracefs>,
                        std::unique_ptr<ProtoTranslationTable>,
                        std::unique_ptr<FtraceConfigMuxer>);

    std::unique_ptr<Tracefs> tracefs;
    std::unique_ptr<ProtoTranslationTable> table;
    std::unique_ptr<FtraceConfigMuxer> ftrace_config_muxer;
    std::vector<CpuReader> cpu_readers;  // empty if no started data sources
    std::set<FtraceDataSource*> started_data_sources;
    // for snapshotting ftrace clock if not using "boot":
    base::ScopedFile cpu_zero_stats_fd;
    // for reading based on ring buffer capacity:
    bool buffer_watches_posted = false;
  };

  FtraceInstanceState* GetInstance(const std::string& instance_name);
  // virtual for testing:
  virtual std::unique_ptr<FtraceInstanceState> CreateSecondaryInstance(
      const std::string& instance_name);

  virtual uint64_t NowMs() const;

  AtraceWrapper* atrace_wrapper() const { return atrace_wrapper_.get(); }

 private:
  friend class TestFtraceController;
  enum class PollSupport { kUntested, kSupported, kUnsupported };

  // Periodic task that reads all per-cpu ftrace buffers. Global across tracefs
  // instances.
  void ReadTick(int generation);
  bool ReadPassForInstance(FtraceInstanceState* instance);
  uint32_t GetTickPeriodMs();
  // Optional: additional reads based on buffer capacity. Per tracefs instance.
  void UpdateBufferWatermarkWatches(FtraceInstanceState* instance,
                                    const std::string& instance_name);
  void OnBufferPastWatermark(const std::string& instance_name,
                             size_t cpu,
                             bool repoll_watermark);
  void RemoveBufferWatermarkWatches(FtraceInstanceState* instance);
  PollSupport VerifyKernelSupportForBufferWatermark();

  void FlushForInstance(FtraceInstanceState* instance);

  void StartIfNeeded(FtraceInstanceState* instance,
                     const std::string& instance_name);
  void StopIfNeeded(FtraceInstanceState* instance);

  FtraceInstanceState* GetOrCreateInstance(const std::string& instance_name);
  void DestroyIfUnusedSeconaryInstance(FtraceInstanceState* instance);

  size_t GetStartedDataSourcesCount();
  std::optional<CpuReader::FtraceClockSnapshot> SnapshotFtraceClockIfNotBoot(
      FtraceInstanceState* instance);

  template <typename F /* void(FtraceInstanceState*) */>
  void ForEachInstance(F fn);

  base::TaskRunner* const task_runner_;
  Observer* const observer_;
  CpuReader::ParsingBuffers parsing_mem_;
  LazyKernelSymbolizer symbolizer_;
  FtraceConfigId next_cfg_id_ = 1;
  int tick_generation_ = 0;
  bool retain_ksyms_on_stop_ = false;
  PollSupport buffer_watermark_support_ = PollSupport::kUntested;
  std::set<FtraceDataSource*> data_sources_;
  std::unique_ptr<AtraceWrapper> atrace_wrapper_;
  // Default tracefs instance (normally /sys/kernel/tracing) is valid for as
  // long as the controller is valid.
  // Secondary instances (i.e. /sys/kernel/tracing/instances/...) are created
  // and destroyed as necessary between AddDataSource and RemoveDataSource:
  FtraceInstanceState primary_;
  std::map<std::string, std::unique_ptr<FtraceInstanceState>>
      secondary_instances_;

  base::WeakPtrFactory<FtraceController> weak_factory_;  // Keep last.
};

bool DumpKprobeStats(std::string text, FtraceStats* ftrace_stats);

}  // namespace perfetto

#endif  // SRC_TRACED_PROBES_FTRACE_FTRACE_CONTROLLER_H_
