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

#include <cstdint>
#include <string>
#include <unordered_map>

#include "perfetto/base/status.h"
#include "src/base/test/status_matchers.h"
#include "src/trace_redaction/collect_timeline_events.h"
#include "src/trace_redaction/find_package_uid.h"
#include "src/trace_redaction/redact_sched_events.h"
#include "src/trace_redaction/trace_redaction_framework.h"
#include "src/trace_redaction/trace_redaction_integration_fixture.h"
#include "src/trace_redaction/trace_redactor.h"
#include "test/gtest_and_gmock.h"

#include "protos/perfetto/trace/ftrace/ftrace_event.pbzero.h"
#include "protos/perfetto/trace/ftrace/ftrace_event_bundle.pbzero.h"
#include "protos/perfetto/trace/ftrace/sched.pbzero.h"
#include "protos/perfetto/trace/trace.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto::trace_redaction {

// >>> SELECT uid
// >>>   FROM package_list
// >>>   WHERE package_name='com.Unity.com.unity.multiplayer.samples.coop'
//
//     +-------+
//     |  uid  |
//     +-------+
//     | 10252 |
//     +-------+
//
// >>> SELECT uid, upid, name
// >>>   FROM process
// >>>   WHERE uid=10252
//
//     +-------+------+----------------------------------------------+
//     |  uid  | upid | name                                         |
//     +-------+------+----------------------------------------------+
//     | 10252 | 843  | com.Unity.com.unity.multiplayer.samples.coop |
//     +-------+------+----------------------------------------------+
//
// >>> SELECT tid, name
// >>>   FROM thread
// >>>   WHERE upid=843 AND name IS NOT NULL
//
//     +------+-----------------+
//     | tid  | name            |
//     +------+-----------------+
//     | 7120 | Binder:7105_2   |
//     | 7127 | UnityMain       |
//     | 7142 | Job.worker 0    |
//     | 7143 | Job.worker 1    |
//     | 7144 | Job.worker 2    |
//     | 7145 | Job.worker 3    |
//     | 7146 | Job.worker 4    |
//     | 7147 | Job.worker 5    |
//     | 7148 | Job.worker 6    |
//     | 7150 | Background Job. |
//     | 7151 | Background Job. |
//     | 7167 | UnityGfxDeviceW |
//     | 7172 | AudioTrack      |
//     | 7174 | FMOD stream thr |
//     | 7180 | Binder:7105_3   |
//     | 7184 | UnityChoreograp |
//     | 7945 | Filter0         |
//     | 7946 | Filter1         |
//     | 7947 | Thread-7        |
//     | 7948 | FMOD mixer thre |
//     | 7950 | UnityGfxDeviceW |
//     | 7969 | UnityGfxDeviceW |
//     +------+-----------------+
class RedactSchedSwitchIntegrationTest
    : public testing::Test,
      protected TraceRedactionIntegrationFixure {
 protected:
  void SetUp() override {
    trace_redactor_.emplace_collect<FindPackageUid>();
    trace_redactor_.emplace_collect<CollectTimelineEvents>();

    auto* redact_sched_events =
        trace_redactor_.emplace_transform<RedactSchedEvents>();
    redact_sched_events->emplace_modifier<ClearComms>();
    redact_sched_events->emplace_waking_filter<AllowAll>();

    context_.package_name = kSomePackageName;
  }

  std::unordered_map<int32_t, std::string> expected_names_ = {
      {7120, "Binder:7105_2"},   {7127, "UnityMain"},
      {7142, "Job.worker 0"},    {7143, "Job.worker 1"},
      {7144, "Job.worker 2"},    {7145, "Job.worker 3"},
      {7146, "Job.worker 4"},    {7147, "Job.worker 5"},
      {7148, "Job.worker 6"},    {7150, "Background Job."},
      {7151, "Background Job."}, {7167, "UnityGfxDeviceW"},
      {7172, "AudioTrack"},      {7174, "FMOD stream thr"},
      {7180, "Binder:7105_3"},   {7184, "UnityChoreograp"},
      {7945, "Filter0"},         {7946, "Filter1"},
      {7947, "Thread-7"},        {7948, "FMOD mixer thre"},
      {7950, "UnityGfxDeviceW"}, {7969, "UnityGfxDeviceW"},
  };

  Context context_;
  TraceRedactor trace_redactor_;
};

TEST_F(RedactSchedSwitchIntegrationTest, ClearsNonTargetSwitchComms) {
  auto result = Redact(trace_redactor_, &context_);
  ASSERT_OK(result) << result.c_message();

  auto original = LoadOriginal();
  ASSERT_OK(original) << original.status().c_message();

  auto redacted = LoadRedacted();
  ASSERT_OK(redacted) << redacted.status().c_message();

  auto redacted_trace_data = LoadRedacted();
  ASSERT_OK(redacted_trace_data) << redacted.status().c_message();

  protos::pbzero::Trace::Decoder decoder(redacted_trace_data.value());

  for (auto packet = decoder.packet(); packet; ++packet) {
    protos::pbzero::TracePacket::Decoder packet_decoder(*packet);

    if (!packet_decoder.has_ftrace_events()) {
      continue;
    }

    protos::pbzero::FtraceEventBundle::Decoder ftrace_events_decoder(
        packet_decoder.ftrace_events());

    for (auto event = ftrace_events_decoder.event(); event; ++event) {
      protos::pbzero::FtraceEvent::Decoder event_decoder(*event);

      if (!event_decoder.has_sched_switch()) {
        continue;
      }

      protos::pbzero::SchedSwitchFtraceEvent::Decoder sched_decoder(
          event_decoder.sched_switch());

      ASSERT_TRUE(sched_decoder.has_next_pid());
      ASSERT_TRUE(sched_decoder.has_next_comm());

      // If the pid is expected, make sure it has the right now. If it is not
      // expected, it should be missing.
      auto next_pid = sched_decoder.next_pid();
      auto next_comm = expected_names_.find(next_pid);

      if (next_comm == expected_names_.end()) {
        ASSERT_EQ(sched_decoder.next_comm().size, 0u);
      } else {
        ASSERT_EQ(sched_decoder.next_comm().ToStdString(), next_comm->second);
      }

      ASSERT_TRUE(sched_decoder.has_prev_pid());
      ASSERT_TRUE(sched_decoder.has_prev_comm());

      auto prev_pid = sched_decoder.prev_pid();
      auto prev_comm = expected_names_.find(prev_pid);

      if (prev_comm == expected_names_.end()) {
        ASSERT_EQ(sched_decoder.prev_comm().size, 0u);
      } else {
        ASSERT_EQ(sched_decoder.prev_comm().ToStdString(), prev_comm->second);
      }
    }
  }
}

}  // namespace perfetto::trace_redaction
