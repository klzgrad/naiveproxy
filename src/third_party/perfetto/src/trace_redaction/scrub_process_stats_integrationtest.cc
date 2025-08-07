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

#include "perfetto/base/status.h"
#include "src/base/test/status_matchers.h"
#include "src/trace_redaction/collect_timeline_events.h"
#include "src/trace_redaction/scrub_process_stats.h"
#include "src/trace_redaction/trace_redaction_framework.h"
#include "src/trace_redaction/trace_redaction_integration_fixture.h"
#include "src/trace_redaction/trace_redactor.h"
#include "test/gtest_and_gmock.h"

#include "protos/perfetto/trace/ps/process_stats.pbzero.h"
#include "protos/perfetto/trace/trace.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto::trace_redaction {

class ScrubProcessStatsTest : public testing::Test,
                              protected TraceRedactionIntegrationFixure {
 protected:
  void SetUp() override {
    trace_redactor_.emplace_collect<CollectTimelineEvents>();

    auto* scrub = trace_redactor_.emplace_transform<ScrubProcessStats>();
    scrub->emplace_filter<ConnectedToPackage>();

    context_.package_uid = kSomePackageUid;
  }

  // Gets pids from all process_stats messages in the trace (bytes).
  base::FlatSet<int32_t> GetAllPids(const std::string& bytes) const {
    base::FlatSet<int32_t> pids;

    protos::pbzero::Trace::Decoder decoder(bytes);

    for (auto packet = decoder.packet(); packet; ++packet) {
      protos::pbzero::TracePacket::Decoder trace_packet(packet->as_bytes());

      if (!trace_packet.has_process_stats()) {
        continue;
      }

      protos::pbzero::ProcessStats::Decoder process_stats(
          trace_packet.process_stats());

      for (auto process = process_stats.processes(); process; ++process) {
        protos::pbzero::ProcessStats::Process::Decoder p(process->as_bytes());
        PERFETTO_DCHECK(p.has_pid());
        pids.insert(p.pid());
      }
    }

    return pids;
  }

  Context context_;
  TraceRedactor trace_redactor_;
};

// This test is a canary for changes to the test data. If the test data was to
// change, every test in this file would fail.
//
//  SELECT DISTINCT pid
//  FROM process
//  WHERE upid IN (
//    SELECT DISTINCT upid
//    FROM counter
//      JOIN process_counter_track ON counter.track_id=process_counter_track.id
//    WHERE name!='oom_score_adj'
//  )
//  ORDER BY pid
//
//  NOTE: WHERE name!='oom_score_adj' is used because there are two sources for
//  oom_score_adj values and we only want process stats here.
TEST_F(ScrubProcessStatsTest, VerifyTraceStats) {
  base::FlatSet<int32_t> expected = {
      1,     578,   581,   696,   697,   698,   699,   700,   701,   704,
      709,   710,   718,   728,   749,   750,   751,   752,   756,   760,
      761,   762,   873,   874,   892,   1046,  1047,  1073,  1074,  1091,
      1092,  1093,  1101,  1103,  1104,  1105,  1106,  1107,  1110,  1111,
      1112,  1113,  1115,  1116,  1118,  1119,  1120,  1121,  1123,  1124,
      1125,  1126,  1127,  1129,  1130,  1131,  1133,  1140,  1145,  1146,
      1147,  1151,  1159,  1163,  1164,  1165,  1166,  1167,  1168,  1175,
      1177,  1205,  1206,  1235,  1237,  1238,  1248,  1251,  1254,  1255,
      1295,  1296,  1298,  1300,  1301,  1303,  1304,  1312,  1317,  1325,
      1339,  1340,  1363,  1374,  1379,  1383,  1388,  1392,  1408,  1409,
      1410,  1413,  1422,  1426,  1427,  1428,  1429,  1433,  1436,  1448,
      1450,  1451,  1744,  1774,  1781,  1814,  2262,  2268,  2286,  2392,
      2456,  2502,  2510,  2518,  2528,  2569,  3171,  3195,  3262,  3286,
      3310,  3338,  3442,  3955,  4386,  4759,  5935,  6034,  6062,  6167,
      6547,  6573,  6720,  6721,  6725,  6944,  6984,  7105,  7207,  7557,
      7636,  7786,  7874,  7958,  7960,  7967,  15449, 15685, 15697, 16453,
      19683, 21124, 21839, 23150, 23307, 23876, 24317, 25017, 25126, 25450,
      25474, 27271, 30604, 32289,
  };

  auto original = LoadOriginal();
  ASSERT_OK(original) << original.status().c_message();

  auto actual = GetAllPids(*original);

  for (auto pid : expected) {
    ASSERT_TRUE(actual.count(pid))
        << "pid " << pid << " was not found in the trace";
  }

  for (auto pid : actual) {
    ASSERT_TRUE(expected.count(pid))
        << "pid " << pid << " was found in the trace";
  }
}

// Package name: "com.Unity.com.unity.multiplayer.samples.coop"
// Package pid: 7105
TEST_F(ScrubProcessStatsTest, OnlyKeepsStatsForPackage) {
  auto result = Redact(trace_redactor_, &context_);
  ASSERT_OK(result) << result.c_message();

  auto redacted = LoadRedacted();
  ASSERT_OK(redacted) << redacted.status().c_message();

  auto actual = GetAllPids(*redacted);
  ASSERT_EQ(actual.size(), 1u);
  ASSERT_TRUE(actual.count(7105));
}

}  // namespace perfetto::trace_redaction
