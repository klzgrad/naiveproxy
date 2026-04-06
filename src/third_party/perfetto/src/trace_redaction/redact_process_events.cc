/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "src/trace_redaction/redact_process_events.h"

#include <string>

#include "perfetto/ext/base/status_macros.h"
#include "perfetto/protozero/scattered_heap_buffer.h"
#include "src/trace_redaction/proto_util.h"

#include "protos/perfetto/trace/ftrace/ftrace_event.pbzero.h"
#include "protos/perfetto/trace/ftrace/ftrace_event_bundle.pbzero.h"
#include "protos/perfetto/trace/ftrace/power.pbzero.h"
#include "protos/perfetto/trace/ftrace/sched.pbzero.h"
#include "protos/perfetto/trace/ftrace/task.pbzero.h"

namespace perfetto::trace_redaction {

base::Status RedactProcessEvents::Transform(const Context& context,
                                            std::string* packet) const {
  PERFETTO_DCHECK(modifier_);
  PERFETTO_DCHECK(filter_);

  if (!context.timeline) {
    return base::ErrStatus("RedactProcessEvents: missing timeline.");
  }

  if (!context.package_uid.has_value()) {
    return base::ErrStatus("RedactProcessEvents: missing package uid.");
  }

  if (!packet || packet->empty()) {
    return base::ErrStatus("RedactProcessEvents: null or empty packet.");
  }

  protozero::ProtoDecoder packet_decoder(*packet);

  protozero::HeapBuffered<protos::pbzero::TracePacket> message;

  for (auto it = packet_decoder.ReadField(); it.valid();
       it = packet_decoder.ReadField()) {
    if (it.id() == protos::pbzero::TracePacket::kFtraceEventsFieldNumber) {
      RETURN_IF_ERROR(
          OnFtraceEvents(context, it.as_bytes(), message->set_ftrace_events()));
    } else {
      proto_util::AppendField(it, message.get());
    }
  }

  packet->assign(message.SerializeAsString());
  return base::OkStatus();
}

base::Status RedactProcessEvents::OnFtraceEvents(
    const Context& context,
    protozero::ConstBytes bytes,
    protos::pbzero::FtraceEventBundle* message) const {
  protozero::ProtoDecoder decoder(bytes);

  auto cpu =
      decoder.FindField(protos::pbzero::FtraceEventBundle::kCpuFieldNumber);

  std::string shared_comm;

  for (auto it = decoder.ReadField(); it.valid(); it = decoder.ReadField()) {
    if (it.id() == protos::pbzero::FtraceEventBundle::kEventFieldNumber) {
      RETURN_IF_ERROR(OnFtraceEvent(context, cpu.as_int32(), it.as_bytes(),
                                    &shared_comm, message->add_event()));
    } else {
      proto_util::AppendField(it, message);
    }
  }

  return base::OkStatus();
}

base::Status RedactProcessEvents::OnFtraceEvent(
    const Context& context,
    int32_t cpu,
    protozero::ConstBytes bytes,
    std::string* shared_comm,
    protos::pbzero::FtraceEvent* message) const {
  PERFETTO_DCHECK(shared_comm);
  PERFETTO_DCHECK(message);

  protozero::ProtoDecoder decoder(bytes);

  auto ts =
      decoder.FindField(protos::pbzero::FtraceEvent::kTimestampFieldNumber);

  if (!ts.valid()) {
    return base::ErrStatus("RedactProcessEvents: missing FtraceEvent %d",
                           protos::pbzero::FtraceEvent::kTimestampFieldNumber);
  }

  auto pid = decoder.FindField(protos::pbzero::FtraceEvent::kPidFieldNumber);

  if (!pid.valid()) {
    return base::ErrStatus("RedactProcessEvents: missing pid %d",
                           protos::pbzero::FtraceEvent::kPidFieldNumber);
  }

  for (auto it = decoder.ReadField(); it.valid(); it = decoder.ReadField()) {
    switch (it.id()) {
      case protos::pbzero::FtraceEvent::kSchedProcessFreeFieldNumber:
        RETURN_IF_ERROR(OnProcessFree(context, ts.as_uint64(), cpu,
                                      it.as_bytes(), shared_comm, message));
        break;
      case protos::pbzero::FtraceEvent::kTaskNewtaskFieldNumber:
        RETURN_IF_ERROR(OnNewTask(context, ts.as_uint64(), cpu, it.as_bytes(),
                                  shared_comm, message));
        break;
      case protos::pbzero::FtraceEvent::kTaskRenameFieldNumber:
        RETURN_IF_ERROR(OnProcessRename(context, ts.as_uint64(), cpu,
                                        pid.as_int32(), it.as_bytes(),
                                        shared_comm, message));
        break;
      case protos::pbzero::FtraceEvent::kPrintFieldNumber:
        RETURN_IF_ERROR(OnPrint(context, ts.as_uint64(), bytes, message));
        break;
      case protos::pbzero::FtraceEvent::kSuspendResumeFieldNumber:
        RETURN_IF_ERROR(
            OnSuspendResume(context, ts.as_uint64(), bytes, message));
        break;
      case protos::pbzero::FtraceEvent::kSchedBlockedReasonFieldNumber:
        RETURN_IF_ERROR(
            OnSchedBlockedReason(context, ts.as_uint64(), bytes, message));
        break;
      default:
        proto_util::AppendField(it, message);
        break;
    }
  }

  return base::OkStatus();
}

base::Status RedactProcessEvents::OnProcessFree(
    const Context& context,
    uint64_t ts,
    int32_t cpu,
    protozero::ConstBytes bytes,
    std::string* shared_comm,
    protos::pbzero::FtraceEvent* parent_message) const {
  PERFETTO_DCHECK(shared_comm);
  PERFETTO_DCHECK(parent_message);

  protos::pbzero::SchedProcessFreeFtraceEvent::Decoder decoder(bytes);

  if (!decoder.has_pid()) {
    return base::ErrStatus(
        "RedactProcessEvents: missing SchedProcessFreeFtraceEvent %d",
        protos::pbzero::SchedProcessFreeFtraceEvent::kPidFieldNumber);
  }

  if (!decoder.has_comm()) {
    return base::ErrStatus(
        "RedactProcessEvents: missing SchedProcessFreeFtraceEvent %d",
        protos::pbzero::SchedProcessFreeFtraceEvent::kCommFieldNumber);
  }

  if (!decoder.has_prio()) {
    return base::ErrStatus(
        "RedactProcessEvents: missing SchedProcessFreeFtraceEvent %d",
        protos::pbzero::SchedProcessFreeFtraceEvent::kPrioFieldNumber);
  }

  auto pid = decoder.pid();
  auto comm = decoder.comm();
  auto prio = decoder.prio();

  PERFETTO_DCHECK(filter_);
  if (!filter_->Includes(context, ts, pid)) {
    return base::OkStatus();
  }

  shared_comm->assign(comm.data, comm.size);

  PERFETTO_DCHECK(modifier_);
  modifier_->Modify(context, ts, cpu, &pid, shared_comm);

  auto* message = parent_message->set_sched_process_free();
  message->set_pid(pid);
  message->set_comm(*shared_comm);
  message->set_prio(prio);

  return base::OkStatus();
}

base::Status RedactProcessEvents::OnNewTask(
    const Context& context,
    uint64_t ts,
    int32_t cpu,
    protozero::ConstBytes bytes,
    std::string* shared_comm,
    protos::pbzero::FtraceEvent* parent_message) const {
  PERFETTO_DCHECK(shared_comm);
  PERFETTO_DCHECK(parent_message);

  protos::pbzero::TaskNewtaskFtraceEvent::Decoder decoder(bytes);

  if (!decoder.has_clone_flags()) {
    return base::ErrStatus(
        "RedactProcessEvents: missing TaskNewtaskFtraceEvent %d",
        protos::pbzero::TaskNewtaskFtraceEvent::kCloneFlagsFieldNumber);
  }

  if (!decoder.has_comm()) {
    return base::ErrStatus(
        "RedactProcessEvents: missing TaskNewtaskFtraceEvent %d",
        protos::pbzero::TaskNewtaskFtraceEvent::kCommFieldNumber);
  }

  if (!decoder.has_oom_score_adj()) {
    return base::ErrStatus(
        "RedactProcessEvents: missing TaskNewtaskFtraceEvent %d",
        protos::pbzero::TaskNewtaskFtraceEvent::kOomScoreAdjFieldNumber);
  }

  if (!decoder.has_pid()) {
    return base::ErrStatus(
        "RedactProcessEvents: missing TaskNewtaskFtraceEvent %d",
        protos::pbzero::TaskNewtaskFtraceEvent::kPidFieldNumber);
  }

  auto clone_flags = decoder.clone_flags();
  auto comm = decoder.comm();
  auto omm_score_adj = decoder.oom_score_adj();
  auto pid = decoder.pid();

  PERFETTO_DCHECK(filter_);
  if (!filter_->Includes(context, ts, pid)) {
    return base::OkStatus();
  }

  shared_comm->assign(comm.data, comm.size);

  PERFETTO_DCHECK(modifier_);
  modifier_->Modify(context, ts, cpu, &pid, shared_comm);

  auto* message = parent_message->set_task_newtask();
  message->set_clone_flags(clone_flags);
  message->set_comm(*shared_comm);
  message->set_oom_score_adj(omm_score_adj);
  message->set_pid(pid);

  return base::OkStatus();
}

// Remove clear contents of RedactProcessEvents messages when the message does
// not belong to the target package.
//
// protos/perfetto/trace/ftrace/task.proto
base::Status RedactProcessEvents::OnProcessRename(
    const Context& context,
    uint64_t ts,
    int32_t cpu,
    int32_t pid,
    protozero::ConstBytes bytes,
    std::string* shared_comm,
    protos::pbzero::FtraceEvent* parent_message) const {
  PERFETTO_DCHECK(shared_comm);
  PERFETTO_DCHECK(parent_message);

  protos::pbzero::TaskRenameFtraceEvent::Decoder decoder(bytes);

  if (!decoder.has_newcomm()) {
    return base::ErrStatus(
        "RedactProcessEvents: missing TaskRenameFtraceEvent new comm (field id "
        "%d)",
        protos::pbzero::TaskRenameFtraceEvent::kNewcommFieldNumber);
  }

  if (!decoder.has_oldcomm()) {
    return base::ErrStatus(
        "RedactProcessEvents: missing TaskRenameFtraceEvent old comm (field id "
        "%d)",
        protos::pbzero::TaskRenameFtraceEvent::kOldcommFieldNumber);
  }

  if (!decoder.has_oom_score_adj()) {
    return base::ErrStatus(
        "RedactProcessEvents: missing TaskRenameFtraceEvent oom score (%d)",
        protos::pbzero::TaskRenameFtraceEvent::kOomScoreAdjFieldNumber);
  }

  auto new_comm = decoder.newcomm();
  auto old_comm = decoder.oldcomm();
  auto oom_score_adj = decoder.oom_score_adj();

  // The rename task's pid *should* always match the ftrace event's pid. To
  // support backwards compatibility but assume the ftrace event's pid can be
  // used, the rename task's pid will be used if it is present, otherwise it'll
  // use the ftrace event's pid.
  //
  // https://b.corp.google.com/issues/407810213
  auto nearest_pid = decoder.has_pid() ? decoder.pid() : pid;

  PERFETTO_DCHECK(filter_);
  if (!filter_->Includes(context, ts, nearest_pid)) {
    return base::OkStatus();
  }

  auto* message = parent_message->set_task_rename();

  shared_comm->assign(old_comm.data, old_comm.size);

  PERFETTO_DCHECK(modifier_);

  // Copy the pid so that, if changed by Modify(), we can test if the pid
  // changed. The modifier here should not change the pid.
  auto old_comm_pid = nearest_pid;
  auto old_comm_string = old_comm.ToStdString();
  modifier_->Modify(context, ts, cpu, &old_comm_pid, &old_comm_string);

  auto new_comm_pid = nearest_pid;
  auto new_comm_string = new_comm.ToStdString();
  modifier_->Modify(context, ts, cpu, &new_comm_pid, &new_comm_string);

  PERFETTO_DCHECK(old_comm_pid == nearest_pid);
  PERFETTO_DCHECK(new_comm_pid == nearest_pid);

  message->set_oldcomm(old_comm_string);
  message->set_newcomm(new_comm_string);
  message->set_oom_score_adj(oom_score_adj);

  return base::OkStatus();
}

base::Status RedactProcessEvents::OnPrint(
    const Context& context,
    uint64_t ts,
    protozero::ConstBytes event_bytes,
    protos::pbzero::FtraceEvent* parent_message) const {
  PERFETTO_DCHECK(parent_message);

  protozero::ProtoDecoder decoder(event_bytes);

  auto pid = decoder.FindField(protos::pbzero::FtraceEvent::kPidFieldNumber);
  if (!pid.valid()) {
    return base::ErrStatus("RedactProcessEvents: missing FtraceEvent %u",
                           pid.id());
  }

  auto print =
      decoder.FindField(protos::pbzero::FtraceEvent::kPrintFieldNumber);
  if (!print.valid()) {
    return base::ErrStatus("RedactProcessEvents: missing FtraceEvent %u",
                           print.id());
  }

  if (filter_->Includes(context, ts, pid.as_int32())) {
    proto_util::AppendField(print, parent_message);
  }

  return base::OkStatus();
}

base::Status RedactProcessEvents::OnSuspendResume(
    const Context& context,
    uint64_t ts,
    protozero::ConstBytes event_bytes,
    protos::pbzero::FtraceEvent* parent_message) const {
  PERFETTO_DCHECK(parent_message);

  // Values are taken from "suspend_period.textproto". These values would
  // ideally be provided via the context, but until there are multiple sources,
  // they can be here.
  constexpr std::array<std::string_view, 3> kValidActions = {
      "syscore_suspend", "syscore_resume", "timekeeping_freeze"};

  protozero::ProtoDecoder decoder(event_bytes);

  auto pid = decoder.FindField(protos::pbzero::FtraceEvent::kPidFieldNumber);
  if (!pid.valid()) {
    return base::ErrStatus("RedactProcessEvents: missing FtraceEvent::kPid");
  }

  auto suspend_resume_field =
      decoder.FindField(protos::pbzero::FtraceEvent::kSuspendResumeFieldNumber);
  if (!suspend_resume_field.valid()) {
    return base::ErrStatus(
        "RedactProcessEvents: missing FtraceEvent::kSuspendResume");
  }

  protos::pbzero::SuspendResumeFtraceEvent::Decoder suspend_resume(
      suspend_resume_field.as_bytes());

  auto action = suspend_resume.action();
  std::string_view action_str(action.data, action.size);

  // Do the allow list first because it should be cheaper (e.g. array look-up vs
  // timeline query).
  if (std::find(kValidActions.begin(), kValidActions.end(), action_str) !=
      kValidActions.end()) {
    if (filter_->Includes(context, ts, pid.as_int32())) {
      proto_util::AppendField(suspend_resume_field, parent_message);
    }
  }

  return base::OkStatus();
}

base::Status RedactProcessEvents::OnSchedBlockedReason(
    const Context& context,
    uint64_t ts,
    protozero::ConstBytes event_bytes,
    protos::pbzero::FtraceEvent* parent_message) const {
  PERFETTO_DCHECK(parent_message);

  protos::pbzero::FtraceEvent::Decoder decoder(event_bytes);

  auto pid = decoder.FindField(protos::pbzero::FtraceEvent::kPidFieldNumber);
  if (!pid.valid()) {
    return base::ErrStatus("RedactProcessEvents: missing FtraceEvent::kPid");
  }

  auto blocked_reason_field = decoder.FindField(
      protos::pbzero::FtraceEvent::kSchedBlockedReasonFieldNumber);
  if (!blocked_reason_field.valid()) {
    return base::ErrStatus(
        "RedactProcessEvents: missing FtraceEvent::kSchedBlockedReason");
  }

  protos::pbzero::SchedBlockedReasonFtraceEvent::Decoder blocking_reason(
      blocked_reason_field.as_bytes());

  auto has_fields = {
      blocking_reason.has_caller(),
      blocking_reason.has_io_wait(),
      blocking_reason.has_pid(),
  };

  if (std::find(has_fields.begin(), has_fields.end(), false) !=
      has_fields.end()) {
    return base::ErrStatus(
        "RedactProcessEvents: missing SchedBlockedReasonFtraceEvent::*");
  }

  // The semantics here is similar to waking events (i.e. event.pid is the
  // blocker, and sched_blocked_reason.pid is the blockee).
  // sched_blocked_reason.pid only has meaning when the pid is not merged. If
  // pid was merged, it could have conflicting blocking events.
  if (filter_->Includes(context, ts, blocking_reason.pid())) {
    proto_util::AppendField(blocked_reason_field, parent_message);
  }

  return base::OkStatus();
}

}  // namespace perfetto::trace_redaction
