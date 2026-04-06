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
#include <vector>

#include "src/base/test/status_matchers.h"
#include "src/trace_redaction/collect_timeline_events.h"
#include "src/trace_redaction/find_package_uid.h"
#include "src/trace_redaction/redact_process_events.h"
#include "src/trace_redaction/trace_redaction_framework.h"
#include "src/trace_redaction/trace_redaction_integration_fixture.h"
#include "src/trace_redaction/trace_redactor.h"
#include "test/gtest_and_gmock.h"

#include "protos/perfetto/trace/ftrace/ftrace_event.pbzero.h"
#include "protos/perfetto/trace/ftrace/ftrace_event_bundle.pbzero.h"
#include "protos/perfetto/trace/trace.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto::trace_redaction {

class RenameEventsTraceRedactorIntegrationTest
    : public testing::Test,
      protected TraceRedactionIntegrationFixure {
 protected:
  void SetUp() override {
    // In order for ScrubTaskRename to work, it needs the timeline. All
    // registered primitives are there to generate the timeline.
    trace_redactor_.emplace_collect<FindPackageUid>();
    trace_redactor_.emplace_collect<CollectTimelineEvents>();

    // Configure the system to drop every rename event not connected to the
    // package.
    auto* redact = trace_redactor_.emplace_transform<RedactProcessEvents>();
    redact->emplace_filter<ConnectedToPackage>();
    redact->emplace_modifier<DoNothing>();

    context_.package_name = kSomePackageName;
  }

  void GetRenamedPids(
      const protos::pbzero::FtraceEventBundle::Decoder& ftrace_events,
      std::vector<uint32_t>* pids) const {
    for (auto event_it = ftrace_events.event(); event_it; ++event_it) {
      protos::pbzero::FtraceEvent::Decoder event(*event_it);

      if (event.has_task_rename()) {
        pids->push_back(event.pid());
      }
    }
  }

  std::vector<uint32_t> GetRenamedPids(const std::string bytes) const {
    std::vector<uint32_t> renamed_pids;

    protos::pbzero::Trace::Decoder trace(bytes);

    for (auto packet_it = trace.packet(); packet_it; ++packet_it) {
      protos::pbzero::TracePacket::Decoder packet_decoder(*packet_it);

      if (packet_decoder.has_ftrace_events()) {
        protos::pbzero::FtraceEventBundle::Decoder ftrace_events(
            packet_decoder.ftrace_events());
        GetRenamedPids(ftrace_events, &renamed_pids);
      }
    }

    return renamed_pids;
  }

  Context context_;
  TraceRedactor trace_redactor_;
};

TEST_F(RenameEventsTraceRedactorIntegrationTest, RemovesUnwantedRenameTasks) {
  ASSERT_OK(Redact(trace_redactor_, &context_));

  auto original = LoadOriginal();
  ASSERT_OK(original);

  auto redacted = LoadRedacted();
  ASSERT_OK(redacted);

  auto pids_before = GetRenamedPids(*original);
  std::sort(pids_before.begin(), pids_before.end());

  ASSERT_EQ(pids_before.size(), 4u);
  ASSERT_EQ(pids_before[0], 7971u);
  ASSERT_EQ(pids_before[1], 7972u);
  ASSERT_EQ(pids_before[2], 7973u);
  ASSERT_EQ(pids_before[3], 7974u);

  auto pids_after = GetRenamedPids(*redacted);
  ASSERT_TRUE(pids_after.empty());
}

}  // namespace perfetto::trace_redaction
