/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "src/traced/probes/ftrace/ftrace_stats.h"

#include "protos/perfetto/trace/ftrace/ftrace_stats.pbzero.h"

namespace perfetto {

void FtraceStats::Write(protos::pbzero::FtraceStats* writer) const {
  for (const FtraceCpuStats& cpu_specific_stats : cpu_stats) {
    cpu_specific_stats.Write(writer->add_cpu_stats());
  }
  writer->set_kernel_symbols_parsed(kernel_symbols_parsed);
  writer->set_kernel_symbols_mem_kb(kernel_symbols_mem_kb);
  if (!setup_errors.atrace_errors.empty())
    writer->set_atrace_errors(setup_errors.atrace_errors);
  for (const std::string& err : setup_errors.unknown_ftrace_events)
    writer->add_unknown_ftrace_events(err);
  for (const std::string& err : setup_errors.failed_ftrace_events)
    writer->add_failed_ftrace_events(err);

  if (kprobe_stats.hits || kprobe_stats.misses) {
    auto* kprobe_stats_pb = writer->set_kprobe_stats();
    kprobe_stats_pb->set_hits(kprobe_stats.hits);
    kprobe_stats_pb->set_misses(kprobe_stats.misses);
  }
}

void FtraceCpuStats::Write(protos::pbzero::FtraceCpuStats* writer) const {
  writer->set_cpu(cpu);
  writer->set_entries(entries);
  writer->set_overrun(overrun);
  writer->set_commit_overrun(commit_overrun);
  writer->set_bytes_read(bytes);
  writer->set_oldest_event_ts(oldest_event_ts);
  writer->set_now_ts(now_ts);
  writer->set_dropped_events(dropped_events);
  writer->set_read_events(read_events);
}

}  // namespace perfetto
