/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "src/traced/probes/ftrace/compact_sched.h"

#include <stdint.h>
#include <optional>

#include "protos/perfetto/config/ftrace/ftrace_config.gen.h"
#include "protos/perfetto/trace/ftrace/ftrace_event.pbzero.h"
#include "protos/perfetto/trace/ftrace/sched.pbzero.h"
#include "src/traced/probes/ftrace/event_info_constants.h"
#include "src/traced/probes/ftrace/ftrace_config_utils.h"

namespace perfetto {

namespace {

// Pre-parse the format of sched_switch, checking if our simplifying
// assumptions about possible widths/signedness hold, and record the subset
// of the format that will be used during parsing.
std::optional<CompactSchedSwitchFormat> ValidateSchedSwitchFormat(
    const Event& event) {
  using protos::pbzero::SchedSwitchFtraceEvent;

  CompactSchedSwitchFormat switch_format;
  switch_format.event_id = event.ftrace_event_id;
  switch_format.size = event.size;

  bool prev_state_valid = false;
  bool next_pid_valid = false;
  bool next_prio_valid = false;
  bool next_comm_valid = false;
  for (const auto& field : event.fields) {
    switch (field.proto_field_id) {
      case SchedSwitchFtraceEvent::kPrevStateFieldNumber:
        switch_format.prev_state_offset = field.ftrace_offset;
        switch_format.prev_state_type = field.ftrace_type;

        // kernel type: long
        prev_state_valid = (field.ftrace_type == kFtraceInt32 ||
                            field.ftrace_type == kFtraceInt64);
        break;

      case SchedSwitchFtraceEvent::kNextPidFieldNumber:
        switch_format.next_pid_offset = field.ftrace_offset;
        switch_format.next_pid_type = field.ftrace_type;

        // kernel type: pid_t
        next_pid_valid = (field.ftrace_type == kFtracePid32);
        break;

      case SchedSwitchFtraceEvent::kNextPrioFieldNumber:
        switch_format.next_prio_offset = field.ftrace_offset;
        switch_format.next_prio_type = field.ftrace_type;

        // kernel type: int
        next_prio_valid = (field.ftrace_type == kFtraceInt32);
        break;

      case SchedSwitchFtraceEvent::kNextCommFieldNumber:
        switch_format.next_comm_offset = field.ftrace_offset;

        next_comm_valid =
            (field.ftrace_type == kFtraceFixedCString &&
             field.ftrace_size == CommInterner::kExpectedCommLength);
        break;
      default:
        break;
    }
  }

  if (!prev_state_valid || !next_pid_valid || !next_prio_valid ||
      !next_comm_valid) {
    return std::nullopt;
  }
  return std::make_optional(switch_format);
}

// Pre-parse the format of sched_waking, checking if our simplifying
// assumptions about possible widths/signedness hold, and record the subset
// of the format that will be used during parsing.
std::optional<CompactSchedWakingFormat> ValidateSchedWakingFormat(
    const Event& event,
    const std::vector<Field>& common_fields) {
  using protos::pbzero::FtraceEvent;
  using protos::pbzero::SchedWakingFtraceEvent;

  CompactSchedWakingFormat waking_format;
  waking_format.event_id = event.ftrace_event_id;
  waking_format.size = event.size;

  bool pid_valid = false;
  bool target_cpu_valid = false;
  bool prio_valid = false;
  bool comm_valid = false;
  bool common_flags_valid = false;

  for (const Field& field : common_fields) {
    if (field.proto_field_id == FtraceEvent::kCommonFlagsFieldNumber) {
      waking_format.common_flags_offset = field.ftrace_offset;
      waking_format.common_flags_type = field.ftrace_type;

      common_flags_valid = (field.ftrace_type == kFtraceUint8);
      break;
    }
  }

  for (const Field& field : event.fields) {
    switch (field.proto_field_id) {
      case SchedWakingFtraceEvent::kPidFieldNumber:
        waking_format.pid_offset = field.ftrace_offset;
        waking_format.pid_type = field.ftrace_type;

        // kernel type: pid_t
        pid_valid = (field.ftrace_type == kFtracePid32);
        break;

      case SchedWakingFtraceEvent::kTargetCpuFieldNumber:
        waking_format.target_cpu_offset = field.ftrace_offset;
        waking_format.target_cpu_type = field.ftrace_type;

        // kernel type: int
        target_cpu_valid = (field.ftrace_type == kFtraceInt32);
        break;

      case SchedWakingFtraceEvent::kPrioFieldNumber:
        waking_format.prio_offset = field.ftrace_offset;
        waking_format.prio_type = field.ftrace_type;

        // kernel type: int
        prio_valid = (field.ftrace_type == kFtraceInt32);
        break;

      case SchedWakingFtraceEvent::kCommFieldNumber:
        waking_format.comm_offset = field.ftrace_offset;

        comm_valid = (field.ftrace_type == kFtraceFixedCString &&
                      field.ftrace_size == CommInterner::kExpectedCommLength);
        break;
      default:
        break;
    }
  }

  if (!pid_valid || !target_cpu_valid || !prio_valid || !comm_valid ||
      !common_flags_valid) {
    return std::nullopt;
  }
  return std::make_optional(waking_format);
}

}  // namespace

// TODO(rsavitski): could avoid looping over all events if the caller did the
// work to remember the relevant events (translation table construction already
// loops over them).
CompactSchedEventFormat ValidateFormatForCompactSched(
    const std::vector<Event>& events,
    const std::vector<Field>& common_fields) {
  using protos::pbzero::FtraceEvent;

  std::optional<CompactSchedSwitchFormat> switch_format;
  std::optional<CompactSchedWakingFormat> waking_format;
  for (const Event& event : events) {
    if (event.proto_field_id == FtraceEvent::kSchedSwitchFieldNumber) {
      switch_format = ValidateSchedSwitchFormat(event);
    }
    if (event.proto_field_id == FtraceEvent::kSchedWakingFieldNumber) {
      waking_format = ValidateSchedWakingFormat(event, common_fields);
    }
  }

  if (switch_format.has_value() && waking_format.has_value()) {
    return CompactSchedEventFormat{/*format_valid=*/true, switch_format.value(),
                                   waking_format.value()};
  } else {
    PERFETTO_ELOG("Unexpected sched_switch or sched_waking format.");
    return CompactSchedEventFormat{/*format_valid=*/false,
                                   CompactSchedSwitchFormat{},
                                   CompactSchedWakingFormat{}};
  }
}

CompactSchedEventFormat InvalidCompactSchedEventFormatForTesting() {
  return CompactSchedEventFormat{/*format_valid=*/false,
                                 CompactSchedSwitchFormat{},
                                 CompactSchedWakingFormat{}};
}

CompactSchedConfig CreateCompactSchedConfig(
    const FtraceConfig& request,
    bool switch_requested,
    const CompactSchedEventFormat& compact_format) {
  // If compile-time assumptions don't hold, we'll fall back onto encoding
  // events individually.
  if (!compact_format.format_valid) {
    return CompactSchedConfig{/*enabled=*/false};
  }
  // Enabled unless we're not recording sched_switch, or explicitly opting out.
  // Note: compact sched_waking depends on sched_switch (for derived
  // common_pid), so use verbose encoding if the config requests only
  // sched_waking.
  const auto& compact = request.compact_sched();
  if (!switch_requested || (compact.has_enabled() && !compact.enabled())) {
    return CompactSchedConfig{/*enabled=*/false};
  }
  return CompactSchedConfig{/*enabled=*/true};
}

CompactSchedConfig EnabledCompactSchedConfigForTesting() {
  return CompactSchedConfig{/*enabled=*/true};
}

CompactSchedConfig DisabledCompactSchedConfigForTesting() {
  return CompactSchedConfig{/*enabled=*/false};
}

void CompactSchedSwitchBuffer::Write(
    protos::pbzero::FtraceEventBundle::CompactSched* compact_out) const {
  compact_out->set_switch_timestamp(timestamp_);
  compact_out->set_switch_next_pid(next_pid_);
  compact_out->set_switch_prev_state(prev_state_);
  compact_out->set_switch_next_prio(next_prio_);
  compact_out->set_switch_next_comm_index(next_comm_index_);
}

void CompactSchedSwitchBuffer::Reset() {
  last_timestamp_ = 0;
  timestamp_.Reset();
  next_pid_.Reset();
  prev_state_.Reset();
  next_prio_.Reset();
  next_comm_index_.Reset();
}

void CompactSchedWakingBuffer::Write(
    protos::pbzero::FtraceEventBundle::CompactSched* compact_out) const {
  compact_out->set_waking_timestamp(timestamp_);
  compact_out->set_waking_pid(pid_);
  compact_out->set_waking_target_cpu(target_cpu_);
  compact_out->set_waking_prio(prio_);
  compact_out->set_waking_comm_index(comm_index_);
  compact_out->set_waking_common_flags(common_flags_);
}

void CompactSchedWakingBuffer::Reset() {
  last_timestamp_ = 0;
  timestamp_.Reset();
  pid_.Reset();
  target_cpu_.Reset();
  prio_.Reset();
  comm_index_.Reset();
  common_flags_.Reset();
}

void CommInterner::Write(
    protos::pbzero::FtraceEventBundle::CompactSched* compact_out) const {
  for (size_t i = 0; i < interned_comms_size_; i++) {
    compact_out->add_intern_table(interned_comms_[i].data(),
                                  interned_comms_[i].size());
  }
}

void CommInterner::Reset() {
  intern_buf_write_pos_ = 0;
  interned_comms_size_ = 0;
}

void CompactSchedBuffer::WriteAndReset(
    protos::pbzero::FtraceEventBundle* bundle) {
  if (switch_.size() > 0 || waking_.size() > 0) {
    auto* compact_out = bundle->set_compact_sched();

    PERFETTO_DCHECK(interner_.interned_comms_size() > 0);
    interner_.Write(compact_out);

    if (switch_.size() > 0)
      switch_.Write(compact_out);

    if (waking_.size() > 0)
      waking_.Write(compact_out);
  }
  Reset();
}

void CompactSchedBuffer::Reset() {
  interner_.Reset();
  switch_.Reset();
  waking_.Reset();
}

}  // namespace perfetto
