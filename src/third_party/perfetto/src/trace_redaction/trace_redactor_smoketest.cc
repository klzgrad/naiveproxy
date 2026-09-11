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

#include "src/base/test/status_matchers.h"
#include "src/trace_redaction/trace_redaction_integration_fixture.h"
#include "src/trace_redaction/trace_redactor.h"
#include "test/gtest_and_gmock.h"

namespace perfetto::trace_redaction {

class TraceRedactorSmokeTests : public testing::Test,
                                protected TraceRedactionIntegrationFixure {
 protected:
  void SetUp() override {
    SetSourceTrace("test/data/trace-redaction-smoke.pftrace");

    context_.package_name = "com.example.sampleapp";
    auto trace_redactor = TraceRedactor::CreateInstance({false});

    ASSERT_OK(Redact(*trace_redactor, &context_));

    auto unredacted_trace_str = LoadOriginal();
    ASSERT_OK(unredacted_trace_str) << unredacted_trace_str.status().message();

    auto redacted_trace_str = LoadRedacted();
    ASSERT_OK(redacted_trace_str) << redacted_trace_str.status().message();

    unredacted_tp_ = CreateTraceProcessor(*unredacted_trace_str);
    ASSERT_NE(unredacted_tp_, nullptr);
    redacted_tp_ = CreateTraceProcessor(*redacted_trace_str);
    ASSERT_NE(redacted_tp_, nullptr);
  }

  std::unique_ptr<trace_processor::TraceProcessor> unredacted_tp_;
  std::unique_ptr<trace_processor::TraceProcessor> redacted_tp_;
  Context context_;
};

TEST_F(TraceRedactorSmokeTests, RedactorKeepsAllSlicesForTargetProcess) {
  const std::string query =
      "INCLUDE PERFETTO MODULE slices.with_context;\n"
      "select COUNT(*) as total_count from thread_or_process_slice where "
      "process_name = 'com.example.sampleapp'";

  auto unredacted_it = unredacted_tp_->ExecuteQuery(query);
  ASSERT_TRUE(unredacted_it.Next());
  auto unredacted_count = unredacted_it.Get(0).AsLong();

  auto redacted_it = redacted_tp_->ExecuteQuery(query);
  ASSERT_TRUE(redacted_it.Next());
  auto redacted_count = redacted_it.Get(0).AsLong();

  // Ensure that the source trace contains slices for target process
  ASSERT_GT(unredacted_count, 0);

  // Ensure that the redacted trace contains the same number of slices for
  // target process
  EXPECT_EQ(unredacted_count, redacted_count);
}

}  // namespace perfetto::trace_redaction
