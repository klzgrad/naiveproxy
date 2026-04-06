/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "perfetto/trace_processor/trace_processor.h"
#include "src/base/test/status_matchers.h"
#include "src/trace_redaction/trace_redaction_integration_fixture.h"
#include "test/gtest_and_gmock.h"

namespace perfetto::trace_redaction {
namespace {
constexpr auto kTrace = "test/data/trace-redaction-perf-sample.pftrace";

constexpr auto kPackageName = "com.example.sampleapp";
constexpr auto kPid = 25131;
}  // namespace

class PrunePerfEventsIntegrationTest
    : public testing::Test,
      protected TraceRedactionIntegrationFixure {
 protected:
  void SetUp() override {
    SetSourceTrace(kTrace);

    TraceRedactor::Config tr_config;
    auto trace_redactor = TraceRedactor::CreateInstance(tr_config);

    Context context;
    context.package_name = kPackageName;
    base::Status status = Redact(*trace_redactor, &context);
    if (!status.ok()) {
      PERFETTO_ELOG("Redaction error: %s", status.c_message());
    }
    ASSERT_OK(status);

    auto raw_original = LoadOriginal();
    ASSERT_OK(raw_original);
    trace_processor_original_ = CreateTraceProcessor(raw_original.value());

    auto raw_redacted = LoadRedacted();
    ASSERT_OK(raw_redacted);
    trace_processor_redacted_ = CreateTraceProcessor(raw_redacted.value());
  }

  std::unique_ptr<trace_processor::TraceProcessor> trace_processor_original_;
  std::unique_ptr<trace_processor::TraceProcessor> trace_processor_redacted_;
};

TEST_F(PrunePerfEventsIntegrationTest, OnlyKeepsTargetProcessPerfSamples) {
  // This query retrieves the total number of perf samples for target process
  // in the original trace.
  auto original_samples_for_process_query =
      "SELECT COUNT(*) FROM perf_sample "
      "JOIN thread ON thread.utid = perf_sample.utid "
      "JOIN process ON process.upid = thread.upid "
      "GROUP BY pid "
      "HAVING pid = " +
      std::to_string(kPid);

  auto original_rows = trace_processor_redacted_->ExecuteQuery(
      original_samples_for_process_query);
  ASSERT_TRUE(original_rows.Next());
  const int64_t perf_samples_for_target_pid = original_rows.Get(0).AsLong();
  ASSERT_TRUE(perf_samples_for_target_pid > 0);

  // This query retrieves the total number of perf samples for all processes
  // in redacted trace
  auto redacted_samples_for_all_processes_query =
      "SELECT COUNT(*) FROM perf_sample "
      "JOIN thread ON thread.utid = perf_sample.utid "
      "JOIN process ON process.upid = thread.upid";
  auto redacted_rows = trace_processor_redacted_->ExecuteQuery(
      redacted_samples_for_all_processes_query);
  ASSERT_TRUE(redacted_rows.Next());
  const int64_t trace_perf_samples = redacted_rows.Get(0).AsLong();

  // Check that all the trace samples in the trace correspond to target process.
  ASSERT_TRUE(perf_samples_for_target_pid == trace_perf_samples);

  ASSERT_OK(redacted_rows.Status());
}

// TODO (edgararriagag): There is currently a bug in the timeline where some
// slices are dropped which affects ability of redactor to pass this test. Once
// b/446994151 is fixed, we should reenable this test.
TEST_F(PrunePerfEventsIntegrationTest,
       DISABLED_TargetProcessPerfSamplesMatchesUnredacted) {
  // This query retrieves the total number of perf samples for target process
  // in the trace.
  const auto total_samples_for_process_query =
      "SELECT COUNT(*) FROM perf_sample "
      "JOIN thread ON thread.utid = perf_sample.utid "
      "JOIN process ON process.upid = thread.upid "
      "GROUP BY pid "
      "HAVING pid = " +
      std::to_string(kPid);

  auto original_rows =
      trace_processor_original_->ExecuteQuery(total_samples_for_process_query);
  ASSERT_TRUE(original_rows.Next());
  const int64_t original_target_process_samples = original_rows.Get(0).AsLong();

  auto redacted_rows =
      trace_processor_redacted_->ExecuteQuery(total_samples_for_process_query);
  ASSERT_TRUE(redacted_rows.Next());
  const int64_t redacted_target_process_samples = redacted_rows.Get(0).AsLong();

  // Check that all the trace samples in the trace correspond to target process.
  ASSERT_TRUE(original_target_process_samples ==
              redacted_target_process_samples);

  ASSERT_OK(redacted_rows.Status());
}

}  // namespace perfetto::trace_redaction
