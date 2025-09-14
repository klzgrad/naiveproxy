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

#include "perfetto/trace_processor/trace_processor.h"
#include "src/base/test/status_matchers.h"
#include "src/trace_redaction/trace_redaction_integration_fixture.h"
#include "test/gtest_and_gmock.h"

namespace perfetto::trace_redaction {
namespace {
constexpr auto kTrace = "test/data/trace-redaction-api-capture.pftrace";

constexpr auto kPackageName = "com.prefabulated.touchlatency";
constexpr auto kPid = 4524;
}  // namespace

class CollectFrameCookiesIntegrationTest
    : public testing::Test,
      protected TraceRedactionIntegrationFixure {
 protected:
  void SetUp() override {
    SetSourceTrace(kTrace);

    trace_processor::Config tp_config;
    trace_processor_ =
        trace_processor::TraceProcessor::CreateInstance(tp_config);

    TraceRedactor::Config tr_config;
    auto trace_redactor = TraceRedactor::CreateInstance(tr_config);

    Context context;
    context.package_name = kPackageName;

    ASSERT_OK(Redact(*trace_redactor, &context));

    auto raw = LoadRedacted();
    ASSERT_OK(raw);

    auto read_buffer = std::make_unique<uint8_t[]>(raw->size());
    memcpy(read_buffer.get(), raw->data(), raw->size());

    ASSERT_OK(trace_processor_->Parse(std::move(read_buffer), raw->size()));
    ASSERT_OK(trace_processor_->NotifyEndOfFile());
  }

  std::unique_ptr<trace_processor::TraceProcessor> trace_processor_;
};

TEST_F(CollectFrameCookiesIntegrationTest, OnlyRetainsTargetActualFrames) {
  auto query =
      " SELECT pid"
      "   FROM process"
      "   WHERE upid IN ("
      "     SELECT DISTINCT upid FROM actual_frame_timeline_slice)";

  auto rows = trace_processor_->ExecuteQuery(query);

  ASSERT_TRUE(rows.Next());
  ASSERT_EQ(rows.Get(0).AsLong(), kPid);

  ASSERT_FALSE(rows.Next());
  ASSERT_OK(rows.Status());
}

TEST_F(CollectFrameCookiesIntegrationTest, OnlyRetainsTargetExpectedFrames) {
  auto query =
      " SELECT pid"
      "   FROM process"
      "   WHERE upid IN ("
      "     SELECT DISTINCT upid FROM expected_frame_timeline_slice)";

  auto row = trace_processor_->ExecuteQuery(query);

  ASSERT_TRUE(row.Next());
  ASSERT_EQ(row.Get(0).AsLong(), kPid);

  ASSERT_FALSE(row.Next());
  ASSERT_OK(row.Status());
}

// The target package has two overlapping timelines. So both tracks should exist
// under one pid.
TEST_F(CollectFrameCookiesIntegrationTest,
       RetainsOverlappingExpectedFrameEvents) {
  auto query =
      " SELECT DISTINCT track_id, pid"
      "   FROM expected_frame_timeline_slice"
      "     JOIN process USING (upid)";

  auto rows = trace_processor_->ExecuteQuery(query);

  ASSERT_TRUE(rows.Next());
  ASSERT_EQ(rows.Get(1).AsLong(), kPid);

  ASSERT_TRUE(rows.Next());
  ASSERT_EQ(rows.Get(1).AsLong(), kPid);

  ASSERT_FALSE(rows.Next());
  ASSERT_OK(rows.Status());
}

}  // namespace perfetto::trace_redaction
