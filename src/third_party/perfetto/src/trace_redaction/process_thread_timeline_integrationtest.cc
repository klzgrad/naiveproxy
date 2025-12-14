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

#include "src/base/test/status_matchers.h"
#include "src/trace_redaction/collect_timeline_events.h"
#include "src/trace_redaction/find_package_uid.h"
#include "src/trace_redaction/trace_redaction_framework.h"
#include "src/trace_redaction/trace_redaction_integration_fixture.h"
#include "src/trace_redaction/trace_redactor.h"
#include "test/gtest_and_gmock.h"

namespace perfetto::trace_redaction {
namespace {
// Every thread in the package stars before the trace and ends after the
// trace, allowing any time to be used for the query. This time is the
// start time of a slice in the trace.
constexpr uint64_t kTime = 6702094223167642;
}  // namespace

class ProcessThreadTimelineIntegrationTest
    : public testing::Test,
      protected TraceRedactionIntegrationFixure {
 protected:
  void SetUp() override {
    context_.package_name = kSomePackageName;

    trace_redactor_.emplace_collect<FindPackageUid>();
    trace_redactor_.emplace_collect<CollectTimelineEvents>();

    ASSERT_OK(Redact(trace_redactor_, &context_));
  }

  Context context_;
  TraceRedactor trace_redactor_;
};

TEST_F(ProcessThreadTimelineIntegrationTest, PackageThreadsAreConnected) {
  // select * from thread where upid in (
  //   select upid from process where uid in (
  //     select uid from package_list where
  //     package_name='com.Unity.com.unity.multiplayer.samples.coop'))

  auto threads = {
      7105, 7111, 7112, 7113, 7114, 7115, 7116, 7117, 7118, 7119, 7120,
      7124, 7125, 7127, 7129, 7130, 7131, 7132, 7133, 7134, 7135, 7136,
      7137, 7139, 7141, 7142, 7143, 7144, 7145, 7146, 7147, 7148, 7149,
      7150, 7151, 7152, 7153, 7154, 7155, 7156, 7157, 7158, 7159, 7160,
      7161, 7162, 7163, 7164, 7165, 7166, 7167, 7171, 7172, 7174, 7178,
      7180, 7184, 7200, 7945, 7946, 7947, 7948, 7950, 7969,
  };

  for (auto pid : threads) {
    // Use EXPECT instead of ASSERT to test all values.
    EXPECT_TRUE(
        context_.timeline->PidConnectsToUid(kTime, pid, *context_.package_uid));
  }
}

TEST_F(ProcessThreadTimelineIntegrationTest, MainThreadIsConnected) {
  // select * from process where uid in (
  //   select uid from package_list where
  //   package_name='com.Unity.com.unity.multiplayer.samples.coop')

  ASSERT_TRUE(
      context_.timeline->PidConnectsToUid(kTime, 7105, *context_.package_uid));
}

TEST_F(ProcessThreadTimelineIntegrationTest,
       DoesNotConnectDisconnectedMainThread) {
  // /vendor/bin/hw/android.hardware.audio.service
  //
  // select * from thread where upid in (
  //   select upid from process where pid=1104)
  //
  // The audio server, like the targe threads, have no start or end time, so
  // using the "whatever" time is okay. Because the audio service is not
  // directly or indirectly connected to the target package, no thread should
  // test as connected.

  auto threads = {
      1104, 1135, 1142, 1169, 1176, 1602,  1609,  1610,
      1617, 1689, 1690, 1692, 2190, 29650, 23020,
  };

  for (auto pid : threads) {
    // Use EXPECT instead of ASSERT to test all values.
    EXPECT_FALSE(
        context_.timeline->PidConnectsToUid(kTime, pid, *context_.package_uid));
  }
}

}  // namespace perfetto::trace_redaction
