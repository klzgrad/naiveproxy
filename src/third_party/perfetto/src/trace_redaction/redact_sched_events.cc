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

#include "src/trace_redaction/redact_sched_events.h"

#include "perfetto/ext/base/status_macros.h"
#include "perfetto/protozero/scattered_heap_buffer.h"
#include "src/trace_redaction/proto_util.h"

#include "protos/perfetto/trace/ftrace/ftrace_event.pbzero.h"
#include "protos/perfetto/trace/ftrace/ftrace_event_bundle.pbzero.h"
#include "protos/perfetto/trace/ftrace/sched.pbzero.h"

namespace perfetto::trace_redaction {

namespace {
bool IsTrue(bool value) {
  return value;
}

// Copy a field from 'decoder' to 'message' if the field can be found. Returns
// false if the field cannot be found.
bool Passthrough(protozero::ProtoDecoder& decoder,
                 uint32_t field_id,
                 protozero::Message* message) {
  auto field = decoder.FindField(field_id);

  if (field.valid()) {
    proto_util::AppendField(field, message);
    return true;
  }

  return false;
}
}  // namespace

int64_t InternTable::Push(const char* data, size_t size) {
  std::string_view outer(data, size);

  for (size_t i = 0; i < interned_comms_.size(); ++i) {
    auto view = interned_comms_[i];

    if (view == outer) {
      return static_cast<int64_t>(i);
    }
  }

  // No room for the new string, reject the request.
  if (comms_length_ + size > comms_.size()) {
    return -1;
  }

  auto* head = comms_.data() + comms_length_;

  // Important note, the null byte is not copied.
  memcpy(head, data, size);
  comms_length_ += size;

  size_t id = interned_comms_.size();
  interned_comms_.emplace_back(head, size);

  return static_cast<int64_t>(id);
}

std::string_view InternTable::Find(size_t index) const {
  if (index < interned_comms_.size()) {
    return interned_comms_[index];
  }

  return {};
}

// Redact sched switch trace events in an ftrace event bundle:
//
//  event {
//    timestamp: 6702093744772646
//    pid: 0
//    sched_switch {
//      prev_comm: "swapper/0"
//      prev_pid: 0
//      prev_prio: 120
//      prev_state: 0
//      next_comm: "writer"
//      next_pid: 23020
//      next_prio: 96
//    }
//  }
//
// In the above message, it should be noted that "event.pid" will always be
// equal to "event.sched_switch.prev_pid".
//
// "ftrace_event_bundle_message" is the ftrace event bundle (contains a
// collection of ftrace event messages) because data in a sched_switch message
// is needed in order to know if the event should be added to the bundle.

base::Status RedactSchedEvents::Transform(const Context& context,
                                          std::string* packet) const {
  PERFETTO_DCHECK(modifier_);
  PERFETTO_DCHECK(waking_filter_);

  if (!context.timeline) {
    return base::ErrStatus("RedactSchedEvents: missing timeline.");
  }

  if (!context.package_uid.has_value()) {
    return base::ErrStatus("RedactSchedEvents: missing package uid.");
  }

  if (!packet || packet->empty()) {
    return base::ErrStatus("RedactSchedEvents: null or empty packet.");
  }

  protozero::HeapBuffered<protos::pbzero::TracePacket> message;
  protozero::ProtoDecoder decoder(*packet);

  for (auto field = decoder.ReadField(); field.valid();
       field = decoder.ReadField()) {
    if (field.id() == protos::pbzero::TracePacket::kFtraceEventsFieldNumber) {
      RETURN_IF_ERROR(
          OnFtraceEvents(context, field, message->set_ftrace_events()));
    } else {
      proto_util::AppendField(field, message.get());
    }
  }

  packet->assign(message.SerializeAsString());

  return base::OkStatus();
}

base::Status RedactSchedEvents::OnFtraceEvents(
    const Context& context,
    protozero::Field ftrace_events,
    protos::pbzero::FtraceEventBundle* message) const {
  PERFETTO_DCHECK(ftrace_events.id() ==
                  protos::pbzero::TracePacket::kFtraceEventsFieldNumber);

  protozero::ProtoDecoder decoder(ftrace_events.as_bytes());

  auto cpu =
      decoder.FindField(protos::pbzero::FtraceEventBundle::kCpuFieldNumber);
  if (!cpu.valid()) {
    return base::ErrStatus(
        "RedactSchedEvents: missing cpu in ftrace event bundle.");
  }

  for (auto field = decoder.ReadField(); field.valid();
       field = decoder.ReadField()) {
    if (field.id() == protos::pbzero::FtraceEventBundle::kEventFieldNumber) {
      RETURN_IF_ERROR(
          OnFtraceEvent(context, cpu.as_int32(), field, message->add_event()));
      continue;
    }

    if (field.id() ==
        protos::pbzero::FtraceEventBundle::kCompactSchedFieldNumber) {
      protos::pbzero::FtraceEventBundle::CompactSched::Decoder comp_sched(
          field.as_bytes());
      RETURN_IF_ERROR(OnCompSched(context, cpu.as_int32(), comp_sched,
                                  message->set_compact_sched()));
      continue;
    }

    proto_util::AppendField(field, message);
  }

  return base::OkStatus();
}

base::Status RedactSchedEvents::OnFtraceEvent(
    const Context& context,
    int32_t cpu,
    protozero::Field ftrace_event,
    protos::pbzero::FtraceEvent* message) const {
  PERFETTO_DCHECK(ftrace_event.id() ==
                  protos::pbzero::FtraceEventBundle::kEventFieldNumber);

  protozero::ProtoDecoder decoder(ftrace_event.as_bytes());

  auto ts =
      decoder.FindField(protos::pbzero::FtraceEvent::kTimestampFieldNumber);
  if (!ts.valid()) {
    return base::ErrStatus(
        "RedactSchedEvents: missing timestamp in ftrace event.");
  }

  std::string scratch_str;

  for (auto field = decoder.ReadField(); field.valid();
       field = decoder.ReadField()) {
    switch (field.id()) {
      case protos::pbzero::FtraceEvent::kSchedSwitchFieldNumber: {
        protos::pbzero::SchedSwitchFtraceEvent::Decoder sched_switch(
            field.as_bytes());
        RETURN_IF_ERROR(OnFtraceEventSwitch(context, ts.as_uint64(), cpu,
                                            sched_switch, &scratch_str,
                                            message->set_sched_switch()));
        break;
      }

      case protos::pbzero::FtraceEvent::kSchedWakingFieldNumber: {
        protos::pbzero::SchedWakingFtraceEvent::Decoder sched_waking(
            field.as_bytes());
        RETURN_IF_ERROR(OnFtraceEventWaking(
            context, ts.as_uint64(), cpu, sched_waking, &scratch_str, message));
        break;
      }

      default: {
        proto_util::AppendField(field, message);
        break;
      }
    }
  }

  return base::OkStatus();
}

base::Status RedactSchedEvents::OnFtraceEventSwitch(
    const Context& context,
    uint64_t ts,
    int32_t cpu,
    protos::pbzero::SchedSwitchFtraceEvent::Decoder& sched_switch,
    std::string* scratch_str,
    protos::pbzero::SchedSwitchFtraceEvent* message) const {
  PERFETTO_DCHECK(modifier_);
  PERFETTO_DCHECK(scratch_str);
  PERFETTO_DCHECK(message);

  std::array<bool, 7> has_fields = {
      sched_switch.has_prev_comm(), sched_switch.has_prev_pid(),
      sched_switch.has_prev_prio(), sched_switch.has_prev_state(),
      sched_switch.has_next_comm(), sched_switch.has_next_pid(),
      sched_switch.has_next_prio()};

  if (!std::all_of(has_fields.begin(), has_fields.end(), IsTrue)) {
    return base::ErrStatus(
        "RedactSchedEvents: missing required SchedSwitchFtraceEvent "
        "field.");
  }

  auto prev_pid = sched_switch.prev_pid();
  auto prev_comm = sched_switch.prev_comm();

  auto next_pid = sched_switch.next_pid();
  auto next_comm = sched_switch.next_comm();

  // There are 7 values in a sched switch message. Since 4 of the 7 can be
  // replaced, it is easier/cleaner to go value-by-value. Go in proto-defined
  // order.

  scratch_str->assign(prev_comm.data, prev_comm.size);

  modifier_->Modify(context, ts, cpu, &prev_pid, scratch_str);

  message->set_prev_comm(*scratch_str);                // FieldNumber = 1
  message->set_prev_pid(prev_pid);                     // FieldNumber = 2
  message->set_prev_prio(sched_switch.prev_prio());    // FieldNumber = 3
  message->set_prev_state(sched_switch.prev_state());  // FieldNumber = 4

  scratch_str->assign(next_comm.data, next_comm.size);

  modifier_->Modify(context, ts, cpu, &next_pid, scratch_str);

  message->set_next_comm(*scratch_str);              // FieldNumber = 5
  message->set_next_pid(next_pid);                   // FieldNumber = 6
  message->set_next_prio(sched_switch.next_prio());  // FieldNumber = 7

  return base::OkStatus();
}

// Redact sched waking trace events in a ftrace event bundle:
//
//  event {
//    timestamp: 6702093787823849
//    pid: 814                      <-- waker
//    sched_waking {
//      comm: "surfaceflinger"
//      pid: 756                    <-- target
//      prio: 97
//      success: 1
//      target_cpu: 2
//    }
//  }
base::Status RedactSchedEvents::OnFtraceEventWaking(
    const Context& context,
    uint64_t ts,
    int32_t cpu,
    protos::pbzero::SchedWakingFtraceEvent::Decoder& sched_waking,
    std::string* scratch_str,
    protos::pbzero::FtraceEvent* parent_message) const {
  PERFETTO_DCHECK(modifier_);
  PERFETTO_DCHECK(scratch_str);
  PERFETTO_DCHECK(parent_message);

  std::array<bool, 5> has_fields = {
      sched_waking.has_comm(), sched_waking.has_pid(), sched_waking.has_prio(),
      sched_waking.has_success(), sched_waking.has_target_cpu()};

  if (!std::all_of(has_fields.begin(), has_fields.end(), IsTrue)) {
    return base::ErrStatus(
        "RedactSchedEvents: missing required SchedWakingFtraceEvent "
        "field.");
  }

  auto pid = sched_waking.pid();

  if (!waking_filter_->Includes(context, ts, pid)) {
    return base::OkStatus();
  }

  auto comm = sched_waking.comm();

  // There are 5 values in a sched switch message. Since 2 of the 5 can be
  // replaced, it is easier/cleaner to go value-by-value. Go in proto-defined
  // order.

  scratch_str->assign(comm.data, comm.size);

  modifier_->Modify(context, ts, cpu, &pid, scratch_str);

  auto message = parent_message->set_sched_waking();
  message->set_comm(*scratch_str);                     // FieldNumber = 1
  message->set_pid(pid);                               // FieldNumber = 2
  message->set_prio(sched_waking.prio());              // FieldNumber = 3
  message->set_success(sched_waking.success());        // FieldNumber = 4
  message->set_target_cpu(sched_waking.target_cpu());  // FieldNumber = 5

  return base::OkStatus();
}

base::Status RedactSchedEvents::OnCompSched(
    const Context& context,
    int32_t cpu,
    protos::pbzero::FtraceEventBundle::CompactSched::Decoder& comp_sched,
    protos::pbzero::FtraceEventBundle::CompactSched* message) const {
  // Populate the intern table once; it will be used by both sched and waking.
  InternTable intern_table;

  for (auto it = comp_sched.intern_table(); it; ++it) {
    auto chars = it->as_string();
    auto index = intern_table.Push(chars.data, chars.size);

    if (index < 0) {
      return base::ErrStatus(
          "RedactSchedEvents: failed to insert string into intern "
          "table.");
    }
  }

  std::array<bool, 5> has_switch_fields = {
      comp_sched.has_switch_timestamp(),
      comp_sched.has_switch_prev_state(),
      comp_sched.has_switch_next_pid(),
      comp_sched.has_switch_next_prio(),
      comp_sched.has_switch_next_comm_index(),
  };

  if (std::any_of(has_switch_fields.begin(), has_switch_fields.end(), IsTrue)) {
    RETURN_IF_ERROR(
        OnCompSchedSwitch(context, cpu, comp_sched, &intern_table, message));
  }

  std::array<bool, 6> has_waking_fields = {
      comp_sched.has_waking_timestamp(),  comp_sched.has_waking_pid(),
      comp_sched.has_waking_target_cpu(), comp_sched.has_waking_prio(),
      comp_sched.has_waking_comm_index(), comp_sched.has_waking_common_flags(),
  };

  if (std::any_of(has_waking_fields.begin(), has_waking_fields.end(), IsTrue)) {
    RETURN_IF_ERROR(
        OnCompactSchedWaking(context, comp_sched, &intern_table, message));
  }

  // IMPORTANT: The intern table can only be added after switch and waking
  // because switch and/or waking can/will modify the intern table.
  for (auto view : intern_table.values()) {
    message->add_intern_table(view.data(), view.size());
  }

  return base::OkStatus();
}

base::Status RedactSchedEvents::OnCompSchedSwitch(
    const Context& context,
    int32_t cpu,
    protos::pbzero::FtraceEventBundle::CompactSched::Decoder& comp_sched,
    InternTable* intern_table,
    protos::pbzero::FtraceEventBundle::CompactSched* message) const {
  PERFETTO_DCHECK(modifier_);
  PERFETTO_DCHECK(message);

  std::array<bool, 6> has_fields = {
      comp_sched.has_intern_table(),
      comp_sched.has_switch_timestamp(),
      comp_sched.has_switch_prev_state(),
      comp_sched.has_switch_next_pid(),
      comp_sched.has_switch_next_prio(),
      comp_sched.has_switch_next_comm_index(),
  };

  if (!std::all_of(has_fields.begin(), has_fields.end(), IsTrue)) {
    return base::ErrStatus(
        "RedactSchedEvents: missing required FtraceEventBundle::CompactSched "
        "switch field.");
  }

  std::string scratch_str;

  protozero::PackedVarInt packed_comm;
  protozero::PackedVarInt packed_pid;

  // The first it_ts value is an absolute value, all other values are delta
  // values.
  uint64_t ts = 0;

  std::array<bool, 3> parse_errors = {false, false, false};

  auto it_ts = comp_sched.switch_timestamp(&parse_errors.at(0));
  auto it_pid = comp_sched.switch_next_pid(&parse_errors.at(1));
  auto it_comm = comp_sched.switch_next_comm_index(&parse_errors.at(2));

  while (it_ts && it_pid && it_comm) {
    ts += *it_ts;

    auto pid = *it_pid;

    auto comm_index = *it_comm;
    auto comm = intern_table->Find(comm_index);

    scratch_str.assign(comm);

    modifier_->Modify(context, ts, cpu, &pid, &scratch_str);

    auto found = intern_table->Push(scratch_str.data(), scratch_str.size());

    if (found < 0) {
      return base::ErrStatus(
          "RedactSchedEvents: failed to insert string into intern table.");
    }

    packed_comm.Append(found);
    packed_pid.Append(pid);

    ++it_ts;
    ++it_pid;
    ++it_comm;
  }

  if (std::any_of(parse_errors.begin(), parse_errors.end(), IsTrue)) {
    return base::ErrStatus(
        "RedactSchedEvents: error reading FtraceEventBundle::CompactSched.");
  }

  if (it_ts || it_pid || it_comm) {
    return base::ErrStatus(
        "RedactSchedEvents: uneven associative arrays in "
        "FtraceEventBundle::CompactSched (switch).");
  }

  message->set_switch_next_pid(packed_pid);
  message->set_switch_next_comm_index(packed_comm);

  // There's a lot of data in a compact sched message. Most of it is packed data
  // and most of the data is not going to change. To avoid unpacking, doing
  // nothing, and then packing... cheat. Find the fields and pass them as opaque
  // blobs.
  //
  // kInternTableFieldNumber:         The intern table will be modified by both
  //                                  switch events and waking events. It will
  //                                  be written elsewhere.
  //
  // kSwitchNextPidFieldNumber:       The switch pid will change during thread
  //                                  merging.
  //
  // kSwitchNextCommIndexFieldNumber: The switch comm value will change when
  //                                  clearing thread names and replaced
  //                                  during thread merging.

  auto passed_through = {
      Passthrough(comp_sched,
                  protos::pbzero::FtraceEventBundle::CompactSched::
                      kSwitchTimestampFieldNumber,
                  message),
      Passthrough(comp_sched,
                  protos::pbzero::FtraceEventBundle::CompactSched::
                      kSwitchPrevStateFieldNumber,
                  message),
      Passthrough(comp_sched,
                  protos::pbzero::FtraceEventBundle::CompactSched::
                      kSwitchNextPrioFieldNumber,
                  message)};

  if (!std::all_of(passed_through.begin(), passed_through.end(), IsTrue)) {
    return base::ErrStatus(
        "RedactSchedEvents: missing required "
        "FtraceEventBundle::CompactSched switch field.");
  }

  return base::OkStatus();
}

base::Status RedactSchedEvents::OnCompactSchedWaking(
    const Context& context,
    protos::pbzero::FtraceEventBundle::CompactSched::Decoder& compact_sched,
    InternTable* intern_table,
    protos::pbzero::FtraceEventBundle::CompactSched* compact_sched_message)
    const {
  protozero::PackedVarInt var_comm_index;
  protozero::PackedVarInt var_common_flags;
  protozero::PackedVarInt var_pid;
  protozero::PackedVarInt var_prio;
  protozero::PackedVarInt var_target_cpu;
  protozero::PackedVarInt var_timestamp;

  // Time is expressed as delta time, for example:
  //
  //         Event: A          B     C      D
  // Absolute Time: 20         30    35     41
  //                |          |     |      |
  //    Delta Time: 20         10    5      6
  //
  // When an event is removed, for example, event B, delta times are off:
  //
  //               Event:  A          *     C      D
  //       Absolute Time: 20         30    35     41
  //                       |          |     |      |
  //          Delta Time: 20          *     5      6
  //                       |                |      |
  // Effective Abs. Time: 20               25     31
  //               Error:  0               10     10
  //
  // To address this issue, delta times are added into a bucket. The bucket is
  // drained each time an event is retained. If an event is dropped, its time
  // is added to the bucket, but the bucket won't be drained until a retained
  // event drains it.
  uint64_t ts_bucket = 0;
  uint64_t ts_absolute = 0;

  std::string comm;

  std::array<bool, 7> parse_errors = {!compact_sched.has_intern_table(),
                                      false,
                                      false,
                                      false,
                                      false,
                                      false,
                                      false};

  // A note on readability, because the waking iterators are the primary focus,
  // they won't have a "waking" prefix.
  auto it_comm_index = compact_sched.waking_comm_index(&parse_errors.at(1));
  auto it_common_flags = compact_sched.waking_common_flags(&parse_errors.at(2));
  auto it_pid = compact_sched.waking_pid(&parse_errors.at(3));
  auto it_prio = compact_sched.waking_prio(&parse_errors.at(4));
  auto it_target_cpu = compact_sched.waking_target_cpu(&parse_errors.at(5));
  auto it_timestamp = compact_sched.waking_timestamp(&parse_errors.at(6));

  while (it_comm_index && it_common_flags && it_pid && it_prio &&
         it_target_cpu && it_timestamp) {
    ts_bucket += *it_timestamp;  // add time to the bucket
    ts_absolute += *it_timestamp;

    if (waking_filter_->Includes(context, ts_absolute, *it_pid)) {
      // Now that the waking event will be kept, it can be modified using the
      // same rules as switch events.
      auto pid = *it_pid;
      comm.assign(intern_table->Find(*it_comm_index));
      modifier_->Modify(context, ts_absolute, *it_target_cpu, &pid, &comm);

      auto comm_it = intern_table->Push(comm.data(), comm.size());

      var_comm_index.Append(comm_it);
      var_common_flags.Append(*it_common_flags);
      var_pid.Append(pid);
      var_prio.Append(*it_prio);
      var_target_cpu.Append(*it_target_cpu);
      var_timestamp.Append(ts_bucket);

      ts_bucket = 0;  // drain the whole bucket.
    }

    ++it_comm_index;
    ++it_common_flags;
    ++it_pid;
    ++it_prio;
    ++it_target_cpu;
    ++it_timestamp;
  }

  if (std::any_of(parse_errors.begin(), parse_errors.end(), IsTrue)) {
    return base::ErrStatus(
        "RedactSchedEvents: failed to parse FtraceEventBundle::CompactSched.");
  }

  if (it_comm_index || it_common_flags || it_pid || it_prio || it_target_cpu ||
      it_timestamp) {
    return base::ErrStatus(
        "RedactSchedEvents: uneven associative arrays in "
        "FtraceEventBundle::CompactSched (waking).");
  }

  compact_sched_message->set_waking_comm_index(var_comm_index);
  compact_sched_message->set_waking_common_flags(var_common_flags);
  compact_sched_message->set_waking_pid(var_pid);
  compact_sched_message->set_waking_prio(var_prio);
  compact_sched_message->set_waking_target_cpu(var_target_cpu);
  compact_sched_message->set_waking_timestamp(var_timestamp);

  return base::OkStatus();
}

}  // namespace perfetto::trace_redaction
