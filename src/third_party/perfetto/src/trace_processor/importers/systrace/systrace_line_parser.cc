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

#include "src/trace_processor/importers/systrace/systrace_line_parser.h"

#include "perfetto/base/status.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/murmur_hash.h"
#include "perfetto/ext/base/string_splitter.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "src/trace_processor/importers/common/event_tracker.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/slice_tracker.h"
#include "src/trace_processor/importers/common/thread_state_tracker.h"
#include "src/trace_processor/importers/common/track_tracker.h"
#include "src/trace_processor/importers/common/tracks.h"
#include "src/trace_processor/importers/common/tracks_common.h"
#include "src/trace_processor/importers/ftrace/binder_tracker.h"
#include "src/trace_processor/importers/ftrace/ftrace_sched_event_tracker.h"
#include "src/trace_processor/importers/systrace/systrace_line.h"
#include "src/trace_processor/importers/systrace/systrace_parser.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/task_state.h"

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>

namespace perfetto::trace_processor {

SystraceLineParser::SystraceLineParser(TraceProcessorContext* ctx)
    : context_(ctx),
      rss_stat_tracker_(context_),
      sched_wakeup_name_id_(ctx->storage->InternString("sched_wakeup")),
      sched_waking_name_id_(ctx->storage->InternString("sched_waking")),
      workqueue_name_id_(ctx->storage->InternString("workqueue")),
      sched_blocked_reason_id_(
          ctx->storage->InternString("sched_blocked_reason")),
      io_wait_id_(ctx->storage->InternString("io_wait")),
      waker_utid_id_(ctx->storage->InternString("waker_utid")),
      unknown_thread_name_id_(ctx->storage->InternString("<...>")) {}

base::Status SystraceLineParser::ParseLine(const SystraceLine& line) {
  const StringId line_task_id{
      context_->storage->InternString(base::StringView(line.task))};
  auto utid = context_->process_tracker->GetOrCreateThread(line.pid);
  context_->process_tracker->UpdateThreadName(
      utid,
      // Ftrace doesn't always know the thread name (see ftrace documentation
      // for saved_cmdlines) so some lines name a process "<...>". Don't use
      // this bogus name for thread naming otherwise a real name from a previous
      // line could be overwritten.
      line_task_id == unknown_thread_name_id_ ? StringId::Null() : line_task_id,
      ThreadNamePriority::kFtrace);

  if (!line.tgid_str.empty() && line.tgid_str != "-----") {
    std::optional<uint32_t> tgid = base::StringToUInt32(line.tgid_str);
    if (tgid) {
      context_->process_tracker->UpdateThread(line.pid, tgid.value());
    }
  }

  base::FlatHashMap<std::string, std::string, base::MurmurHash<std::string>>
      args;
  for (base::StringSplitter ss(line.args_str, ' '); ss.Next();) {
    std::string key;
    std::string value;
    if (!base::Contains(ss.cur_token(), "=")) {
      key = "name";
      value = ss.cur_token();
      args.Insert(std::move(key), std::move(value));
      continue;
    }
    for (base::StringSplitter inner(ss.cur_token(), '='); inner.Next();) {
      if (key.empty()) {
        key = inner.cur_token();
      } else {
        value = inner.cur_token();
      }
    }
    args.Insert(std::move(key), std::move(value));
  }
  if (line.event_name == "sched_switch") {
    auto prev_state_str = args["prev_state"];
    int64_t prev_state =
        ftrace_utils::TaskState::FromSystrace(prev_state_str.c_str())
            .ToRawStateOnlyForSystraceConversions();

    auto prev_pid = base::StringToUInt32(args["prev_pid"]);
    auto prev_comm = base::StringView(args["prev_comm"]);
    auto prev_prio = base::StringToInt32(args["prev_prio"]);
    auto next_pid = base::StringToUInt32(args["next_pid"]);
    auto next_comm = base::StringView(args["next_comm"]);
    auto next_prio = base::StringToInt32(args["next_prio"]);

    if (!(prev_pid.has_value() && prev_prio.has_value() &&
          next_pid.has_value() && next_prio.has_value())) {
      return base::Status("Could not parse sched_switch");
    }

    FtraceSchedEventTracker::GetOrCreate(context_)->PushSchedSwitch(
        line.cpu, line.ts, prev_pid.value(), prev_comm, prev_prio.value(),
        prev_state, next_pid.value(), next_comm, next_prio.value());
  } else if (line.event_name == "tracing_mark_write" ||
             line.event_name == "0" || line.event_name == "print") {
    SystraceParser::GetOrCreate(context_)->ParsePrintEvent(
        line.ts, line.pid, line.args_str.c_str());
  } else if (line.event_name == "sched_waking") {
    auto comm = args["comm"];
    std::optional<uint32_t> wakee_pid = base::StringToUInt32(args["pid"]);
    if (!wakee_pid.has_value()) {
      return base::Status("Could not convert wakee_pid");
    }

    StringId name_id = context_->storage->InternString(base::StringView(comm));
    auto wakee_utid =
        context_->process_tracker->GetOrCreateThread(wakee_pid.value());
    context_->process_tracker->UpdateThreadName(wakee_utid, name_id,
                                                ThreadNamePriority::kFtrace);

    ThreadStateTracker::GetOrCreate(context_)->PushWakingEvent(
        line.ts, wakee_utid, utid);

  } else if (line.event_name == "cpu_frequency") {
    std::optional<uint32_t> event_cpu = base::StringToUInt32(args["cpu_id"]);
    std::optional<double> new_state = base::StringToDouble(args["state"]);
    if (!event_cpu.has_value()) {
      return base::Status("Could not convert event cpu");
    }
    if (!event_cpu.has_value()) {
      return base::Status("Could not convert state");
    }

    TrackId track = context_->track_tracker->InternTrack(
        tracks::kCpuFrequencyBlueprint, tracks::Dimensions(event_cpu.value()));
    context_->event_tracker->PushCounter(line.ts, new_state.value(), track);
  } else if (line.event_name == "cpu_frequency_limits") {
    std::optional<uint32_t> event_cpu = base::StringToUInt32(args["cpu_id"]);
    if (!event_cpu.has_value()) {
      return base::Status("Could not convert event cpu");
    }
    std::optional<double> max_freq =
        args.Find("max") != nullptr ? base::StringToDouble(args["max"])
                                    : base::StringToDouble(args["max_freq"]);
    std::optional<double> min_freq =
        args.Find("min") != nullptr ? base::StringToDouble(args["min"])
                                    : base::StringToDouble(args["min_freq"]);
    if (max_freq.has_value()) {
      TrackId max_track = context_->track_tracker->InternTrack(
          tracks::kCpuMaxFrequencyLimitBlueprint,
          tracks::Dimensions(event_cpu.value()));
      context_->event_tracker->PushCounter(
          line.ts, static_cast<double>(max_freq.value()), max_track);
    }
    if (min_freq.has_value()) {
      TrackId min_track = context_->track_tracker->InternTrack(
          tracks::kCpuMinFrequencyLimitBlueprint,
          tracks::Dimensions(event_cpu.value()));
      context_->event_tracker->PushCounter(
          line.ts, static_cast<double>(min_freq.value()), min_track);
    }
    if (!max_freq.has_value() && !min_freq.has_value()) {
      return base::Status("Could not convert both max_freq and min_freq");
    }
  } else if (line.event_name == "cpu_idle") {
    std::optional<uint32_t> event_cpu = base::StringToUInt32(args["cpu_id"]);
    std::optional<double> new_state = base::StringToDouble(args["state"]);
    if (!event_cpu.has_value()) {
      return base::Status("Could not convert event cpu");
    }
    if (!event_cpu.has_value()) {
      return base::Status("Could not convert state");
    }

    TrackId track = context_->track_tracker->InternTrack(
        tracks::kCpuIdleBlueprint, tracks::Dimensions(event_cpu.value()));
    context_->event_tracker->PushCounter(line.ts, new_state.value(), track);
  } else if (line.event_name == "binder_transaction") {
    auto id = base::StringToInt32(args["transaction"]);
    auto dest_node = base::StringToInt32(args["dest_node"]);
    auto dest_tgid = base::StringToUInt32(args["dest_proc"]);
    auto dest_tid = base::StringToUInt32(args["dest_thread"]);
    auto is_reply = base::StringToInt32(args["reply"]).value() == 1;
    auto flags_str = args["flags"];
    char* end;
    uint32_t flags = static_cast<uint32_t>(strtol(flags_str.c_str(), &end, 16));
    std::string code_str = args["code"] + " Java Layer Dependent";
    StringId code = context_->storage->InternString(base::StringView(code_str));
    if (!dest_tgid.has_value()) {
      return base::Status("Could not convert dest_tgid");
    }
    if (!dest_tid.has_value()) {
      return base::Status("Could not convert dest_tid");
    }
    if (!id.has_value()) {
      return base::Status("Could not convert transaction id");
    }
    if (!dest_node.has_value()) {
      return base::Status("Could not covert dest node");
    }
    BinderTracker::GetOrCreate(context_)->Transaction(
        line.ts, line.pid, id.value(), dest_node.value(), dest_tgid.value(),
        dest_tid.value(), is_reply, flags, code);
  } else if (line.event_name == "binder_transaction_received") {
    auto id = base::StringToInt32(args["transaction"]);
    if (!id.has_value()) {
      return base::Status("Could not convert transaction id");
    }
    BinderTracker::GetOrCreate(context_)->TransactionReceived(line.ts, line.pid,
                                                              id.value());
  } else if (line.event_name == "binder_command") {
    auto id = base::StringToUInt32(args["cmd"], 0);
    if (!id.has_value()) {
      return base::Status("Could not convert cmd ");
    }
    BinderTracker::GetOrCreate(context_)->CommandToKernel(line.ts, line.pid,
                                                          id.value());
  } else if (line.event_name == "binder_return") {
    auto id = base::StringToUInt32(args["cmd"], 0);
    if (!id.has_value()) {
      return base::Status("Could not convert cmd");
    }
    BinderTracker::GetOrCreate(context_)->ReturnFromKernel(line.ts, line.pid,
                                                           id.value());
  } else if (line.event_name == "binder_lock") {
    BinderTracker::GetOrCreate(context_)->Lock(line.ts, line.pid);
  } else if (line.event_name == "binder_locked") {
    BinderTracker::GetOrCreate(context_)->Locked(line.ts, line.pid);
  } else if (line.event_name == "binder_unlock") {
    BinderTracker::GetOrCreate(context_)->Unlock(line.ts, line.pid);
  } else if (line.event_name == "binder_transaction_alloc_buf") {
    auto data_size = base::StringToUInt64(args["data_size"]);
    auto offsets_size = base::StringToUInt64(args["offsets_size"]);
    if (!data_size.has_value()) {
      return base::Status("Could not convert data size");
    }
    if (!offsets_size.has_value()) {
      return base::Status("Could not convert offsets size");
    }
    BinderTracker::GetOrCreate(context_)->TransactionAllocBuf(
        line.ts, line.pid, data_size.value(), offsets_size.value());
  } else if (line.event_name == "clock_set_rate") {
    auto rate = base::StringToUInt32(args["state"]);
    if (!rate.has_value()) {
      return base::Status("Could not convert state");
    }
    TrackId track = context_->track_tracker->InternTrack(
        tracks::kClockFrequencyBlueprint,
        tracks::Dimensions(base::StringView(args["name"])));
    context_->event_tracker->PushCounter(line.ts, rate.value(), track);
  } else if (line.event_name == "clock_enable" ||
             line.event_name == "clock_disable") {
    auto rate = base::StringToUInt32(args["state"]);
    if (!rate.has_value()) {
      return base::Status("Could not convert state");
    }
    TrackId track = context_->track_tracker->InternTrack(
        tracks::kClockStateBlueprint,
        tracks::Dimensions(base::StringView(args["name"])));
    context_->event_tracker->PushCounter(line.ts, rate.value(), track);
  } else if (line.event_name == "workqueue_execute_start") {
    auto split = base::SplitString(line.args_str, "function ");
    StringId name_id =
        context_->storage->InternString(base::StringView(split[1]));
    TrackId track = context_->track_tracker->InternThreadTrack(utid);
    context_->slice_tracker->Begin(line.ts, track, workqueue_name_id_, name_id);
  } else if (line.event_name == "workqueue_execute_end") {
    TrackId track = context_->track_tracker->InternThreadTrack(utid);
    context_->slice_tracker->End(line.ts, track, workqueue_name_id_);
  } else if (line.event_name == "thermal_temperature") {
    TrackId track = context_->track_tracker->InternTrack(
        tracks::kThermalTemperatureBlueprint,
        tracks::Dimensions(base::StringView(args["thermal_zone"])));
    auto temp = base::StringToInt32(args["temp"]);
    if (!temp.has_value()) {
      return base::Status("Could not convert temp");
    }
    context_->event_tracker->PushCounter(line.ts, temp.value(), track);
  } else if (line.event_name == "cdev_update") {
    TrackId track = context_->track_tracker->InternTrack(
        tracks::kCoolingDeviceCounterBlueprint,
        tracks::Dimensions(base::StringView(args["type"])));
    auto target = base::StringToDouble(args["target"]);
    if (!target.has_value()) {
      return base::Status("Could not convert target");
    }
    context_->event_tracker->PushCounter(line.ts, target.value(), track);
  } else if (line.event_name == "sched_blocked_reason") {
    auto wakee_pid = base::StringToUInt32(args["pid"]);
    if (!wakee_pid.has_value()) {
      return base::Status("sched_blocked_reason: could not parse wakee_pid");
    }
    auto wakee_utid = context_->process_tracker->GetOrCreateThread(*wakee_pid);
    auto io_wait = base::StringToInt32(args["iowait"]);
    if (!io_wait.has_value()) {
      return base::Status("sched_blocked_reason: could not parse io_wait");
    }
    StringId blocked_function =
        context_->storage->InternString(base::StringView(args["caller"]));
    ThreadStateTracker::GetOrCreate(context_)->PushBlockedReason(
        wakee_utid, static_cast<bool>(*io_wait), blocked_function);
  } else if (line.event_name == "rss_stat") {
    // Format: rss_stat: size=8437760 member=1 curr=1 mm_id=2824390453
    auto size = base::StringToInt64(args["size"]);
    auto member = base::StringToUInt32(args["member"]);
    auto mm_id = base::StringToInt64(args["mm_id"]);
    auto opt_curr = base::StringToUInt32(args["curr"]);
    if (!size.has_value()) {
      return base::Status("rss_stat: could not parse size");
    }
    if (!member.has_value()) {
      return base::Status("rss_stat: could not parse member");
    }
    std::optional<bool> curr;
    if (!opt_curr.has_value()) {
      curr = std::make_optional(static_cast<bool>(*opt_curr));
    }
    rss_stat_tracker_.ParseRssStat(line.ts, line.pid, *size, *member, curr,
                                   mm_id);
  }

  return base::OkStatus();
}

}  // namespace perfetto::trace_processor
