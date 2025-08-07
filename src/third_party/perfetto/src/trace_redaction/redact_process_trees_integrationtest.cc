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

#include <string>
#include <vector>

#include "src/base/test/status_matchers.h"
#include "src/trace_redaction/collect_system_info.h"
#include "src/trace_redaction/collect_timeline_events.h"
#include "src/trace_redaction/find_package_uid.h"
#include "src/trace_redaction/redact_process_trees.h"
#include "src/trace_redaction/trace_redaction_framework.h"
#include "src/trace_redaction/trace_redaction_integration_fixture.h"
#include "src/trace_redaction/trace_redactor.h"
#include "test/gtest_and_gmock.h"

#include "protos/perfetto/trace/ps/process_tree.pbzero.h"
#include "protos/perfetto/trace/trace.pbzero.h"

namespace perfetto::trace_redaction {

class RedactProcessTreesIntegrationTest
    : public testing::Test,
      protected TraceRedactionIntegrationFixure {
 protected:
  void SetUp() override {
    trace_redactor_.emplace_collect<CollectSystemInfo>();
    trace_redactor_.emplace_build<BuildSyntheticThreads>();

    trace_redactor_.emplace_collect<FindPackageUid>();
    trace_redactor_.emplace_collect<CollectTimelineEvents>();

    // Filter the process tree based on whether or not a process is part of the
    // target package.
    auto* process_tree =
        trace_redactor_.emplace_transform<RedactProcessTrees>();
    process_tree->emplace_modifier<ProcessTreeDoNothing>();
    process_tree->emplace_filter<ConnectedToPackage>();

    // In this case, the process and package have the same name.
    context_.package_name = kSomePackageName;
  }

  std::unordered_set<int32_t> GetPids(const std::string& bytes) const {
    std::unordered_set<int32_t> pids;

    protos::pbzero::Trace::Decoder decoder(bytes);

    for (auto it = decoder.packet(); it; ++it) {
      protos::pbzero::TracePacket::Decoder packet(*it);

      if (packet.has_process_tree()) {
        GetPids(packet.process_tree(), &pids);
      }
    }

    return pids;
  }

  std::unordered_set<int32_t> GetTids(const std::string& bytes) const {
    std::unordered_set<int32_t> tids;

    protos::pbzero::Trace::Decoder decoder(bytes);

    for (auto it = decoder.packet(); it; ++it) {
      protos::pbzero::TracePacket::Decoder packet(*it);

      if (packet.has_process_tree()) {
        GetTids(packet.process_tree(), &tids);
      }
    }

    return tids;
  }

  Context context_;
  TraceRedactor trace_redactor_;

 private:
  void GetPids(protozero::ConstBytes bytes,
               std::unordered_set<int32_t>* pids) const {
    protos::pbzero::ProcessTree::Decoder process_tree(bytes);

    for (auto it = process_tree.processes(); it; ++it) {
      protos::pbzero::ProcessTree::Process::Decoder process(*it);
      pids->insert(process.ppid());
      pids->insert(process.pid());
    }
  }

  void GetTids(protozero::ConstBytes bytes,
               std::unordered_set<int32_t>* tids) const {
    protos::pbzero::ProcessTree::Decoder process_tree(bytes);

    for (auto it = process_tree.threads(); it; ++it) {
      protos::pbzero::ProcessTree::Thread::Decoder thread(*it);
      tids->insert(thread.tgid());
      tids->insert(thread.tid());
    }
  }
};

TEST_F(RedactProcessTreesIntegrationTest, FilterProcesses) {
  ASSERT_OK(Redact(trace_redactor_, &context_));

  auto original_trace_str = LoadOriginal();
  ASSERT_OK(original_trace_str);

  auto redacted_trace_str = LoadRedacted();
  ASSERT_OK(redacted_trace_str);

  auto original_pids = GetPids(*original_trace_str);
  auto redacted_pids = GetPids(*redacted_trace_str);

  // There are 902 unique pids across all process trees:
  //    grep 'processes {' -A 1  src.pftrace.txt | grep 'pid: ' | grep -Po "\d+"
  //    | sort | uniq | wc -l
  //
  // But if ppids are included, there are 903 pids in the process tree:
  //    grep 'processes {' -A 2  src.pftrace.txt | grep 'pid: ' | grep -Po "\d+"
  //    | sort | uniq | wc -l
  //
  // The above grep statements use a stringified version of the trace. Using "-A
  // 1" will return the pid line. Using "-A 2" will include both pid and ppid.
  //
  // The original process count aligns with trace processor. However, the
  // redacted count does not. The final tree has one process but trace processor
  // reports 4 processes.
  ASSERT_EQ(original_pids.size(), 903u);
  ASSERT_EQ(redacted_pids.size(), 2u);

  ASSERT_TRUE(redacted_pids.count(7105));
}

TEST_F(RedactProcessTreesIntegrationTest, FilterThreads) {
  ASSERT_OK(Redact(trace_redactor_, &context_));

  auto original_trace_str = LoadOriginal();
  ASSERT_OK(original_trace_str);

  auto redacted_trace_str = LoadRedacted();
  ASSERT_OK(redacted_trace_str);

  auto original_tids = GetTids(*original_trace_str);
  auto redacted_tids = GetTids(*redacted_trace_str);

  // There are 2761 unique tids across all process trees:
  //    grep 'threads {' -A 1  src.pftrace.txt | grep 'tid: ' | grep -Po "\d+" |
  //    sort | uniq | wc -l
  //
  // There are 2896 unique tids/tgis across all process trees:
  //    grep 'threads {' -A 2  src.pftrace.txt | grep -P '(tid|tgid): ' | grep
  //    -Po '\d+' | sort | uniq | wc -l
  //
  // The original tid count does NOT align with what trace processor returns.
  // Trace processor reports 3666 threads. The assumption is trace processor is
  // fulling thread information from additional.
  //
  // The redacted tid+tgid count does NOT align with what trace processor
  // returns. Trace processor reports 199 tids where are there are only 63 tids
  // found in process tree. This suggests that trace processor is pulling tid
  // data from other locations.
  ASSERT_EQ(original_tids.size(), 2896u);
  ASSERT_EQ(redacted_tids.size(), 64u);
}

TEST_F(RedactProcessTreesIntegrationTest, AddSynthProcess) {
  // Append another primitive that won't filter, but will add new threads. This
  // will be compatible with the other instanced in SetUp().
  auto* process_tree = trace_redactor_.emplace_transform<RedactProcessTrees>();
  process_tree->emplace_modifier<ProcessTreeCreateSynthThreads>();
  process_tree->emplace_filter<AllowAll>();

  ASSERT_OK(Redact(trace_redactor_, &context_));

  auto redacted_trace_str = LoadRedacted();
  ASSERT_OK(redacted_trace_str);

  auto redacted_pids = GetPids(*redacted_trace_str);

  const auto* synth_process = context_.synthetic_process.get();
  ASSERT_TRUE(synth_process);

  ASSERT_NE(std::find(redacted_pids.begin(), redacted_pids.end(),
                      synth_process->tgid()),
            redacted_pids.end());
}

TEST_F(RedactProcessTreesIntegrationTest, AddSynthThreads) {
  // Append another primitive that won't filter, but will add new threads. This
  // will be compatible with the other instanced in SetUp().
  auto* process_tree = trace_redactor_.emplace_transform<RedactProcessTrees>();
  process_tree->emplace_modifier<ProcessTreeCreateSynthThreads>();
  process_tree->emplace_filter<AllowAll>();

  ASSERT_OK(Redact(trace_redactor_, &context_));

  const auto* synth_process = context_.synthetic_process.get();
  ASSERT_TRUE(synth_process);

  ASSERT_FALSE(synth_process->tids().empty());

  auto original_trace_str = LoadOriginal();
  ASSERT_OK(original_trace_str);

  auto original_tids = GetTids(*original_trace_str);

  // The synth threads should not be found in the original trace.
  for (auto tid : synth_process->tids()) {
    ASSERT_FALSE(original_tids.count(tid));
  }

  auto redacted_trace_str = LoadRedacted();
  ASSERT_OK(redacted_trace_str);

  auto redacted_tids = GetTids(*redacted_trace_str);

  // The synth threads should be found in the redacted trace.
  for (auto tid : synth_process->tids()) {
    ASSERT_TRUE(redacted_tids.count(tid));
  }
}

}  // namespace perfetto::trace_redaction
