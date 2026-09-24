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

#include "perfetto/base/status.h"
#include "src/base/test/status_matchers.h"
#include "src/trace_redaction/add_synth_threads_to_process_trees.h"
#include "src/trace_redaction/collect_system_info.h"
#include "src/trace_redaction/collect_timeline_events.h"
#include "src/trace_redaction/find_package_uid.h"
#include "src/trace_redaction/reduce_threads_in_process_trees.h"
#include "src/trace_redaction/trace_redaction_integration_fixture.h"
#include "src/trace_redaction/trace_redactor.h"
#include "test/gtest_and_gmock.h"

#include "protos/perfetto/trace/ps/process_tree.pbzero.h"
#include "protos/perfetto/trace/trace.pbzero.h"

namespace perfetto::trace_redaction {
namespace {

std::vector<protozero::ConstBytes> GetProcessTrees(const std::string& trace) {
  std::vector<protozero::ConstBytes> tids;

  protos::pbzero::Trace::Decoder decoder(trace);

  for (auto it = decoder.packet(); it; ++it) {
    protos::pbzero::TracePacket::Decoder packet(*it);

    if (packet.has_process_tree()) {
      tids.push_back(packet.process_tree());
    }
  }

  return tids;
}

void AddPidsTo(protozero::ConstBytes bytes, std::vector<int32_t>* pids) {
  protos::pbzero::ProcessTree::Decoder process_tree(bytes);

  for (auto it = process_tree.processes(); it; ++it) {
    protos::pbzero::ProcessTree::Process::Decoder process(*it);
    pids->push_back(process.pid());
  }
}

std::vector<int32_t> GetPids(
    const std::vector<protozero::ConstBytes>& process_trees) {
  std::vector<int32_t> pids;

  for (const auto& process_tree : process_trees) {
    AddPidsTo(process_tree, &pids);
  }

  return pids;
}

void AddTidsTo(protozero::ConstBytes bytes, std::vector<int32_t>* tids) {
  protos::pbzero::ProcessTree::Decoder process_tree(bytes);

  for (auto it = process_tree.threads(); it; ++it) {
    protos::pbzero::ProcessTree::Thread::Decoder thread(*it);
    tids->push_back(thread.tid());
  }
}

std::vector<int32_t> GetTids(
    const std::vector<protozero::ConstBytes>& process_trees) {
  std::vector<int32_t> tids;

  for (const auto& process_tree : process_trees) {
    AddTidsTo(process_tree, &tids);
  }

  return tids;
}

}  // namespace

class RedactProcessTreesIntegrationTest
    : public testing::Test,
      protected TraceRedactionIntegrationFixure {
 protected:
  void SetUp() override {
    context_.package_name = kSomePackageName;

    // We know that the UID should be `package_uid`, but because this is an
    // integration test, we'll rely on primitives rather than explicitly
    // settings values.
    trace_redactor_.emplace_collect<FindPackageUid>();

    trace_redactor_.emplace_collect<CollectTimelineEvents>();

    // Build the synth threads for the source package. In order to create synth
    // threads, system we need system info in order to know what thread ids are
    // in use.
    trace_redactor_.emplace_collect<CollectSystemInfo>();
    trace_redactor_.emplace_build<BuildSyntheticThreads>();

    trace_redactor_.emplace_transform<ReduceThreadsInProcessTrees>();
    trace_redactor_.emplace_transform<AddSythThreadsToProcessTrees>();
  }

  TraceRedactor trace_redactor_;
  Context context_;
};

TEST_F(RedactProcessTreesIntegrationTest, FilterProcesses) {
  ASSERT_OK(Redact(trace_redactor_, &context_));
  ASSERT_OK_AND_ASSIGN(auto original_trace_str, LoadOriginal());
  ASSERT_OK_AND_ASSIGN(auto redacted_trace_str, LoadRedacted());

  auto original_process_trees = GetProcessTrees(original_trace_str);
  auto original_pids = GetPids(original_process_trees);

  auto redacted_process_trees = GetProcessTrees(redacted_trace_str);
  auto redacted_pids = GetPids(redacted_process_trees);

  ASSERT_EQ(original_process_trees.size(), 9u);
  ASSERT_EQ(redacted_process_trees.size(), 9u);

  ASSERT_EQ(std::find(original_pids.begin(), original_pids.end(), 0),
            original_pids.end());

  // Trace processor will say there are 904 threads, but that includes pid 0
  // which never appears in a process tree.
  ASSERT_EQ(original_pids.size(), 903u);

  // TODO(vaage): The number of redacted processes is the main process (appears
  // once) and the synth processes (one per process tree instance). This is
  // wrong, there should only be two, the main process once and the synth
  // process once.

  ASSERT_EQ(redacted_pids.size(), 10u);

  ASSERT_EQ(std::count(redacted_pids.begin(), redacted_pids.end(), 7105), 1);
  ASSERT_EQ(std::count(redacted_pids.begin(), redacted_pids.end(), 4194305), 9);
}

TEST_F(RedactProcessTreesIntegrationTest, FilterThreads) {
  ASSERT_OK(Redact(trace_redactor_, &context_));
  ASSERT_OK_AND_ASSIGN(auto original_trace_str, LoadOriginal());
  ASSERT_OK_AND_ASSIGN(auto redacted_trace_str, LoadRedacted());

  auto original_process_trees = GetProcessTrees(original_trace_str);
  auto original_tids = GetTids(original_process_trees);

  auto redacted_process_trees = GetProcessTrees(redacted_trace_str);
  auto redacted_tids = GetTids(redacted_process_trees);

  ASSERT_EQ(original_process_trees.size(), 9u);
  ASSERT_EQ(redacted_process_trees.size(), 9u);

  // Trace processor says there are 3666 threads. This is the number of
  // processes and threads. In the process trees, they are not combined, so
  // there are 3666 - 903 threads (2763 threads).
  //
  // Trace processor reports a tid 0, but that thread does not appear in the
  // process tree as a thread nor process. That means there are actually
  // 3666 - 903 - 1 threads (2762 threads).
  ASSERT_EQ(original_tids.size(), 2762u);

  // TODO(vaage): There are 72 threads (9 * 8) because 8 synth threads are
  // added to each process tree. They should only be added to the process
  // tree.
  //
  // The remaining 63 threads are from the target process (Unity game). Trace
  // Processor says there are 64, but that's because Trace Process includes the
  // process in the threads list; process trees don't do that.
  ASSERT_EQ(redacted_tids.size(), 135u);
}

}  // namespace perfetto::trace_redaction
