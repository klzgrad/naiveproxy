/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "src/trace_processor/importers/ninja/ninja_log_parser.h"

#include <stdio.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/string_splitter.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/slice_tracker.h"
#include "src/trace_processor/importers/common/track_tracker.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"

namespace perfetto::trace_processor {

using base::StringSplitter;

NinjaLogParser::NinjaLogParser(TraceProcessorContext* ctx) : ctx_(ctx) {}
NinjaLogParser::~NinjaLogParser() = default;

base::Status NinjaLogParser::Parse(TraceBlobView blob) {
  // A trace is read in chunks of arbitrary size (for http fetch() pipeliniing),
  // not necessarily aligned on a line boundary.
  // Here we push everything into a vector and, on each call, consume only
  // the leading part until the last \n, keeping the rest for the next call.
  const char* src = reinterpret_cast<const char*>(blob.data());
  log_.insert(log_.end(), src, src + blob.size());

  // Find the last \n.
  size_t valid_size = log_.size();
  for (; valid_size > 0 && log_[valid_size - 1] != '\n'; --valid_size) {
  }

  for (StringSplitter line(log_.data(), valid_size, '\n'); line.Next();) {
    static const char kHeader[] = "# ninja log v";
    if (!header_parsed_) {
      if (!base::StartsWith(line.cur_token(), kHeader))
        return base::ErrStatus("Failed to parse ninja log header");
      header_parsed_ = true;
      auto version = base::CStringToUInt32(line.cur_token() + strlen(kHeader));
      if (!version || *version != 5)
        return base::ErrStatus("Unsupported ninja log version");
      continue;
    }

    // Each line in the ninja log looks like this:
    // 4 12  1579224178  ui/assets/modal.scss  832a958a9e234dfa
    // Where:
    // - [4, 12] are the timestamps in ms of [start, end] of the job, measured
    //     from the beginning of the build.
    // - 1579224178 is the "restat" (ignored).
    // - ui/assets/modal.scss is the name of the output file being built.
    // - 832a958a9e234dfa is a hash of the compiler invocation.
    // In most cases, each hash should be unique per ninja invocation (because
    // two rules shouln't generate the same output). However, in rare
    // circumstances the same hash can show up more than once. Examples:
    // - A GN action generates > 1 output per invocation (e.g., protos). In this
    //   case all items will have the same [start, end] timestamp. In this case
    //   we want to merge all the output names into one build step, because from
    //   the build system viewpoint, that was the same compiler/tool invocation.
    // - A subtle script that generates different outputs without taking a
    //   --output=filename argument (e.g. via env vars or similar). Note that
    //   this happens in the perfetto codebase itself (goto.google.com/nigew).
    //   In this case we want to treat the two entries as two distinct jobs.
    //
    // In summary the deduping logic here is: if both the hash and the
    // timestamps match -> merge, if not, keep distinct.
    StringSplitter tok(&line, '\t');
    auto t_start = base::CStringToInt64(tok.Next() ? tok.cur_token() : "");
    auto t_end = base::CStringToInt64(tok.Next() ? tok.cur_token() : "");
    tok.Next();  // Ignore restat.
    const char* name = tok.Next() ? tok.cur_token() : nullptr;
    auto cmdhash = base::CStringToUInt64(tok.Next() ? tok.cur_token() : "", 16);

    if (!t_start || !t_end || !name || !cmdhash) {
      ctx_->storage->IncrementStats(stats::ninja_parse_errors);
      continue;
    }

    // If more hashes show up back-to-back with the same timestamps, merge them
    // together as they identify multiple outputs for the same build rule.
    // TODO(lalitm): this merging should really happen in NotifyEndOfFile
    // because we want to merge across builds. However, this needs some
    // non-significant rework of this class so it's not been found to be worth
    // implementing yet.
    if (!jobs_.empty() && *cmdhash == jobs_.back().hash &&
        *t_start == jobs_.back().start_ms && *t_end == jobs_.back().end_ms) {
      jobs_.back().names.append(" ");
      jobs_.back().names.append(name);
      continue;
    }

    jobs_.emplace_back(*t_start, *t_end, *cmdhash, name);
  }
  log_.erase(log_.begin(), log_.begin() + static_cast<ssize_t>(valid_size));
  return base::OkStatus();
}

// This is called after the last Parse() call. At this point all |jobs_| have
// been populated.
base::Status NinjaLogParser::NotifyEndOfFile() {
  std::sort(jobs_.begin(), jobs_.end(),
            [](const Job& x, const Job& y) { return x.start_ms < y.start_ms; });

  // Now we need to work out the job parallelism. There's no direct indication
  // of that in the ninja logs, so it must be inferred by observing overlapping
  // of timestamps. In this context a "Worker" is an inferred sequence of jobs
  // that happened concurrently with other sequences.
  // Here we pack jobs according the following heuristic, for the sake of making
  // the graph nicer to read to humans. Consider the initial situation:
  // 1: [  job 1 ]
  // 2:   [   job 2   ]
  // 3: [   job 3   ]
  //    T=0              | T=6
  // Assume that a new job starts at T=6. It's very likely that job4 was started
  // as a consequence of job2 completion (otherwise it could have been started
  // earlier, soon after job 1 or Job 3). It seems to make more sense to draw
  // it next in the 2nd worker, i.e. next to job 2.
  struct Worker {
    int64_t busy_until;
    TrackId track_id;
  };
  std::vector<Worker> workers;
  for (const auto& job : jobs_) {
    Worker* worker = nullptr;
    for (Worker& cur : workers) {
      // Pick the worker which has the greatest end time (busy_until) <= the
      // job's start time.
      if (cur.busy_until <= job.start_ms) {
        if (!worker || worker->busy_until < cur.busy_until)
          worker = &cur;
      }
    }
    if (worker) {
      // Update the worker's end time with the newly assigned job.
      worker->busy_until = job.end_ms;
    } else {
      static constexpr uint32_t kSyntheticNinjaPid = 1;

      // All workers are busy, allocate a new one.
      uint32_t worker_id = static_cast<uint32_t>(workers.size()) + 1;
      ctx_->process_tracker->SetProcessNameIfUnset(
          ctx_->process_tracker->GetOrCreateProcess(kSyntheticNinjaPid),
          ctx_->storage->InternString("Build"));
      auto utid =
          ctx_->process_tracker->UpdateThread(worker_id, kSyntheticNinjaPid);

      base::StackString<32> name("Worker");
      StringId name_id = ctx_->storage->InternString(name.string_view());
      ctx_->process_tracker->UpdateThreadName(utid, name_id,
                                              ThreadNamePriority::kOther);
      TrackId track_id = ctx_->track_tracker->InternThreadTrack(utid);
      workers.emplace_back(Worker{/*busy_until=*/job.end_ms, track_id});
      worker = &workers.back();
    }

    static constexpr int64_t kMsToNs = 1000ul * 1000;
    const int64_t start_ns = job.start_ms * kMsToNs;
    const int64_t dur_ns = (job.end_ms - job.start_ms) * kMsToNs;
    StringId name_id = ctx_->storage->InternString(base::StringView(job.names));
    ctx_->slice_tracker->Scoped(start_ns, worker->track_id, StringId::Null(),
                                name_id, dur_ns);
  }
  return base::OkStatus();
}

}  // namespace perfetto::trace_processor
