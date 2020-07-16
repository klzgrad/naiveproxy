// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/congestion_control/windowed_filter.h"

#include "net/third_party/quiche/src/quic/core/congestion_control/rtt_stats.h"
#include "net/third_party/quiche/src/quic/core/quic_bandwidth.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"

namespace quic {
namespace test {

class WindowedFilterTest : public QuicTest {
 public:
  // Set the window to 99ms, so 25ms is more than a quarter rtt.
  WindowedFilterTest()
      : windowed_min_rtt_(QuicTime::Delta::FromMilliseconds(99),
                          QuicTime::Delta::Zero(),
                          QuicTime::Zero()),
        windowed_max_bw_(QuicTime::Delta::FromMilliseconds(99),
                         QuicBandwidth::Zero(),
                         QuicTime::Zero()) {}

  // Sets up windowed_min_rtt_ to have the following values:
  // Best = 20ms, recorded at 25ms
  // Second best = 40ms, recorded at 75ms
  // Third best = 50ms, recorded at 100ms
  void InitializeMinFilter() {
    QuicTime now = QuicTime::Zero();
    QuicTime::Delta rtt_sample = QuicTime::Delta::FromMilliseconds(10);
    for (int i = 0; i < 5; ++i) {
      windowed_min_rtt_.Update(rtt_sample, now);
      QUIC_VLOG(1) << "i: " << i << " sample: " << rtt_sample.ToMilliseconds()
                   << " mins: "
                   << " " << windowed_min_rtt_.GetBest().ToMilliseconds() << " "
                   << windowed_min_rtt_.GetSecondBest().ToMilliseconds() << " "
                   << windowed_min_rtt_.GetThirdBest().ToMilliseconds();
      now = now + QuicTime::Delta::FromMilliseconds(25);
      rtt_sample = rtt_sample + QuicTime::Delta::FromMilliseconds(10);
    }
    EXPECT_EQ(QuicTime::Delta::FromMilliseconds(20),
              windowed_min_rtt_.GetBest());
    EXPECT_EQ(QuicTime::Delta::FromMilliseconds(40),
              windowed_min_rtt_.GetSecondBest());
    EXPECT_EQ(QuicTime::Delta::FromMilliseconds(50),
              windowed_min_rtt_.GetThirdBest());
  }

  // Sets up windowed_max_bw_ to have the following values:
  // Best = 900 bps, recorded at 25ms
  // Second best = 700 bps, recorded at 75ms
  // Third best = 600 bps, recorded at 100ms
  void InitializeMaxFilter() {
    QuicTime now = QuicTime::Zero();
    QuicBandwidth bw_sample = QuicBandwidth::FromBitsPerSecond(1000);
    for (int i = 0; i < 5; ++i) {
      windowed_max_bw_.Update(bw_sample, now);
      QUIC_VLOG(1) << "i: " << i << " sample: " << bw_sample.ToBitsPerSecond()
                   << " maxs: "
                   << " " << windowed_max_bw_.GetBest().ToBitsPerSecond() << " "
                   << windowed_max_bw_.GetSecondBest().ToBitsPerSecond() << " "
                   << windowed_max_bw_.GetThirdBest().ToBitsPerSecond();
      now = now + QuicTime::Delta::FromMilliseconds(25);
      bw_sample = bw_sample - QuicBandwidth::FromBitsPerSecond(100);
    }
    EXPECT_EQ(QuicBandwidth::FromBitsPerSecond(900),
              windowed_max_bw_.GetBest());
    EXPECT_EQ(QuicBandwidth::FromBitsPerSecond(700),
              windowed_max_bw_.GetSecondBest());
    EXPECT_EQ(QuicBandwidth::FromBitsPerSecond(600),
              windowed_max_bw_.GetThirdBest());
  }

 protected:
  WindowedFilter<QuicTime::Delta,
                 MinFilter<QuicTime::Delta>,
                 QuicTime,
                 QuicTime::Delta>
      windowed_min_rtt_;
  WindowedFilter<QuicBandwidth,
                 MaxFilter<QuicBandwidth>,
                 QuicTime,
                 QuicTime::Delta>
      windowed_max_bw_;
};

namespace {
// Test helper function: updates the filter with a lot of small values in order
// to ensure that it is not susceptible to noise.
void UpdateWithIrrelevantSamples(
    WindowedFilter<uint64_t, MaxFilter<uint64_t>, uint64_t, uint64_t>* filter,
    uint64_t max_value,
    uint64_t time) {
  for (uint64_t i = 0; i < 1000; i++) {
    filter->Update(i % max_value, time);
  }
}
}  // namespace

TEST_F(WindowedFilterTest, UninitializedEstimates) {
  EXPECT_EQ(QuicTime::Delta::Zero(), windowed_min_rtt_.GetBest());
  EXPECT_EQ(QuicTime::Delta::Zero(), windowed_min_rtt_.GetSecondBest());
  EXPECT_EQ(QuicTime::Delta::Zero(), windowed_min_rtt_.GetThirdBest());
  EXPECT_EQ(QuicBandwidth::Zero(), windowed_max_bw_.GetBest());
  EXPECT_EQ(QuicBandwidth::Zero(), windowed_max_bw_.GetSecondBest());
  EXPECT_EQ(QuicBandwidth::Zero(), windowed_max_bw_.GetThirdBest());
}

TEST_F(WindowedFilterTest, MonotonicallyIncreasingMin) {
  QuicTime now = QuicTime::Zero();
  QuicTime::Delta rtt_sample = QuicTime::Delta::FromMilliseconds(10);
  windowed_min_rtt_.Update(rtt_sample, now);
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(10), windowed_min_rtt_.GetBest());

  // Gradually increase the rtt samples and ensure the windowed min rtt starts
  // rising.
  for (int i = 0; i < 6; ++i) {
    now = now + QuicTime::Delta::FromMilliseconds(25);
    rtt_sample = rtt_sample + QuicTime::Delta::FromMilliseconds(10);
    windowed_min_rtt_.Update(rtt_sample, now);
    QUIC_VLOG(1) << "i: " << i << " sample: " << rtt_sample.ToMilliseconds()
                 << " mins: "
                 << " " << windowed_min_rtt_.GetBest().ToMilliseconds() << " "
                 << windowed_min_rtt_.GetSecondBest().ToMilliseconds() << " "
                 << windowed_min_rtt_.GetThirdBest().ToMilliseconds();
    if (i < 3) {
      EXPECT_EQ(QuicTime::Delta::FromMilliseconds(10),
                windowed_min_rtt_.GetBest());
    } else if (i == 3) {
      EXPECT_EQ(QuicTime::Delta::FromMilliseconds(20),
                windowed_min_rtt_.GetBest());
    } else if (i < 6) {
      EXPECT_EQ(QuicTime::Delta::FromMilliseconds(40),
                windowed_min_rtt_.GetBest());
    }
  }
}

TEST_F(WindowedFilterTest, MonotonicallyDecreasingMax) {
  QuicTime now = QuicTime::Zero();
  QuicBandwidth bw_sample = QuicBandwidth::FromBitsPerSecond(1000);
  windowed_max_bw_.Update(bw_sample, now);
  EXPECT_EQ(QuicBandwidth::FromBitsPerSecond(1000), windowed_max_bw_.GetBest());

  // Gradually decrease the bw samples and ensure the windowed max bw starts
  // decreasing.
  for (int i = 0; i < 6; ++i) {
    now = now + QuicTime::Delta::FromMilliseconds(25);
    bw_sample = bw_sample - QuicBandwidth::FromBitsPerSecond(100);
    windowed_max_bw_.Update(bw_sample, now);
    QUIC_VLOG(1) << "i: " << i << " sample: " << bw_sample.ToBitsPerSecond()
                 << " maxs: "
                 << " " << windowed_max_bw_.GetBest().ToBitsPerSecond() << " "
                 << windowed_max_bw_.GetSecondBest().ToBitsPerSecond() << " "
                 << windowed_max_bw_.GetThirdBest().ToBitsPerSecond();
    if (i < 3) {
      EXPECT_EQ(QuicBandwidth::FromBitsPerSecond(1000),
                windowed_max_bw_.GetBest());
    } else if (i == 3) {
      EXPECT_EQ(QuicBandwidth::FromBitsPerSecond(900),
                windowed_max_bw_.GetBest());
    } else if (i < 6) {
      EXPECT_EQ(QuicBandwidth::FromBitsPerSecond(700),
                windowed_max_bw_.GetBest());
    }
  }
}

TEST_F(WindowedFilterTest, SampleChangesThirdBestMin) {
  InitializeMinFilter();
  // RTT sample lower than the third-choice min-rtt sets that, but nothing else.
  QuicTime::Delta rtt_sample =
      windowed_min_rtt_.GetThirdBest() - QuicTime::Delta::FromMilliseconds(5);
  // This assert is necessary to avoid triggering -Wstrict-overflow
  // See crbug/616957
  ASSERT_GT(windowed_min_rtt_.GetThirdBest(),
            QuicTime::Delta::FromMilliseconds(5));
  // Latest sample was recorded at 100ms.
  QuicTime now = QuicTime::Zero() + QuicTime::Delta::FromMilliseconds(101);
  windowed_min_rtt_.Update(rtt_sample, now);
  EXPECT_EQ(rtt_sample, windowed_min_rtt_.GetThirdBest());
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(40),
            windowed_min_rtt_.GetSecondBest());
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(20), windowed_min_rtt_.GetBest());
}

TEST_F(WindowedFilterTest, SampleChangesThirdBestMax) {
  InitializeMaxFilter();
  // BW sample higher than the third-choice max sets that, but nothing else.
  QuicBandwidth bw_sample =
      windowed_max_bw_.GetThirdBest() + QuicBandwidth::FromBitsPerSecond(50);
  // Latest sample was recorded at 100ms.
  QuicTime now = QuicTime::Zero() + QuicTime::Delta::FromMilliseconds(101);
  windowed_max_bw_.Update(bw_sample, now);
  EXPECT_EQ(bw_sample, windowed_max_bw_.GetThirdBest());
  EXPECT_EQ(QuicBandwidth::FromBitsPerSecond(700),
            windowed_max_bw_.GetSecondBest());
  EXPECT_EQ(QuicBandwidth::FromBitsPerSecond(900), windowed_max_bw_.GetBest());
}

TEST_F(WindowedFilterTest, SampleChangesSecondBestMin) {
  InitializeMinFilter();
  // RTT sample lower than the second-choice min sets that and also
  // the third-choice min.
  QuicTime::Delta rtt_sample =
      windowed_min_rtt_.GetSecondBest() - QuicTime::Delta::FromMilliseconds(5);
  // This assert is necessary to avoid triggering -Wstrict-overflow
  // See crbug/616957
  ASSERT_GT(windowed_min_rtt_.GetSecondBest(),
            QuicTime::Delta::FromMilliseconds(5));
  // Latest sample was recorded at 100ms.
  QuicTime now = QuicTime::Zero() + QuicTime::Delta::FromMilliseconds(101);
  windowed_min_rtt_.Update(rtt_sample, now);
  EXPECT_EQ(rtt_sample, windowed_min_rtt_.GetThirdBest());
  EXPECT_EQ(rtt_sample, windowed_min_rtt_.GetSecondBest());
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(20), windowed_min_rtt_.GetBest());
}

TEST_F(WindowedFilterTest, SampleChangesSecondBestMax) {
  InitializeMaxFilter();
  // BW sample higher than the second-choice max sets that and also
  // the third-choice max.
  QuicBandwidth bw_sample =
      windowed_max_bw_.GetSecondBest() + QuicBandwidth::FromBitsPerSecond(50);
  // Latest sample was recorded at 100ms.
  QuicTime now = QuicTime::Zero() + QuicTime::Delta::FromMilliseconds(101);
  windowed_max_bw_.Update(bw_sample, now);
  EXPECT_EQ(bw_sample, windowed_max_bw_.GetThirdBest());
  EXPECT_EQ(bw_sample, windowed_max_bw_.GetSecondBest());
  EXPECT_EQ(QuicBandwidth::FromBitsPerSecond(900), windowed_max_bw_.GetBest());
}

TEST_F(WindowedFilterTest, SampleChangesAllMins) {
  InitializeMinFilter();
  // RTT sample lower than the first-choice min-rtt sets that and also
  // the second and third-choice mins.
  QuicTime::Delta rtt_sample =
      windowed_min_rtt_.GetBest() - QuicTime::Delta::FromMilliseconds(5);
  // This assert is necessary to avoid triggering -Wstrict-overflow
  // See crbug/616957
  ASSERT_GT(windowed_min_rtt_.GetBest(), QuicTime::Delta::FromMilliseconds(5));
  // Latest sample was recorded at 100ms.
  QuicTime now = QuicTime::Zero() + QuicTime::Delta::FromMilliseconds(101);
  windowed_min_rtt_.Update(rtt_sample, now);
  EXPECT_EQ(rtt_sample, windowed_min_rtt_.GetThirdBest());
  EXPECT_EQ(rtt_sample, windowed_min_rtt_.GetSecondBest());
  EXPECT_EQ(rtt_sample, windowed_min_rtt_.GetBest());
}

TEST_F(WindowedFilterTest, SampleChangesAllMaxs) {
  InitializeMaxFilter();
  // BW sample higher than the first-choice max sets that and also
  // the second and third-choice maxs.
  QuicBandwidth bw_sample =
      windowed_max_bw_.GetBest() + QuicBandwidth::FromBitsPerSecond(50);
  // Latest sample was recorded at 100ms.
  QuicTime now = QuicTime::Zero() + QuicTime::Delta::FromMilliseconds(101);
  windowed_max_bw_.Update(bw_sample, now);
  EXPECT_EQ(bw_sample, windowed_max_bw_.GetThirdBest());
  EXPECT_EQ(bw_sample, windowed_max_bw_.GetSecondBest());
  EXPECT_EQ(bw_sample, windowed_max_bw_.GetBest());
}

TEST_F(WindowedFilterTest, ExpireBestMin) {
  InitializeMinFilter();
  QuicTime::Delta old_third_best = windowed_min_rtt_.GetThirdBest();
  QuicTime::Delta old_second_best = windowed_min_rtt_.GetSecondBest();
  QuicTime::Delta rtt_sample =
      old_third_best + QuicTime::Delta::FromMilliseconds(5);
  // Best min sample was recorded at 25ms, so expiry time is 124ms.
  QuicTime now = QuicTime::Zero() + QuicTime::Delta::FromMilliseconds(125);
  windowed_min_rtt_.Update(rtt_sample, now);
  EXPECT_EQ(rtt_sample, windowed_min_rtt_.GetThirdBest());
  EXPECT_EQ(old_third_best, windowed_min_rtt_.GetSecondBest());
  EXPECT_EQ(old_second_best, windowed_min_rtt_.GetBest());
}

TEST_F(WindowedFilterTest, ExpireBestMax) {
  InitializeMaxFilter();
  QuicBandwidth old_third_best = windowed_max_bw_.GetThirdBest();
  QuicBandwidth old_second_best = windowed_max_bw_.GetSecondBest();
  QuicBandwidth bw_sample =
      old_third_best - QuicBandwidth::FromBitsPerSecond(50);
  // Best max sample was recorded at 25ms, so expiry time is 124ms.
  QuicTime now = QuicTime::Zero() + QuicTime::Delta::FromMilliseconds(125);
  windowed_max_bw_.Update(bw_sample, now);
  EXPECT_EQ(bw_sample, windowed_max_bw_.GetThirdBest());
  EXPECT_EQ(old_third_best, windowed_max_bw_.GetSecondBest());
  EXPECT_EQ(old_second_best, windowed_max_bw_.GetBest());
}

TEST_F(WindowedFilterTest, ExpireSecondBestMin) {
  InitializeMinFilter();
  QuicTime::Delta old_third_best = windowed_min_rtt_.GetThirdBest();
  QuicTime::Delta rtt_sample =
      old_third_best + QuicTime::Delta::FromMilliseconds(5);
  // Second best min sample was recorded at 75ms, so expiry time is 174ms.
  QuicTime now = QuicTime::Zero() + QuicTime::Delta::FromMilliseconds(175);
  windowed_min_rtt_.Update(rtt_sample, now);
  EXPECT_EQ(rtt_sample, windowed_min_rtt_.GetThirdBest());
  EXPECT_EQ(rtt_sample, windowed_min_rtt_.GetSecondBest());
  EXPECT_EQ(old_third_best, windowed_min_rtt_.GetBest());
}

TEST_F(WindowedFilterTest, ExpireSecondBestMax) {
  InitializeMaxFilter();
  QuicBandwidth old_third_best = windowed_max_bw_.GetThirdBest();
  QuicBandwidth bw_sample =
      old_third_best - QuicBandwidth::FromBitsPerSecond(50);
  // Second best max sample was recorded at 75ms, so expiry time is 174ms.
  QuicTime now = QuicTime::Zero() + QuicTime::Delta::FromMilliseconds(175);
  windowed_max_bw_.Update(bw_sample, now);
  EXPECT_EQ(bw_sample, windowed_max_bw_.GetThirdBest());
  EXPECT_EQ(bw_sample, windowed_max_bw_.GetSecondBest());
  EXPECT_EQ(old_third_best, windowed_max_bw_.GetBest());
}

TEST_F(WindowedFilterTest, ExpireAllMins) {
  InitializeMinFilter();
  QuicTime::Delta rtt_sample =
      windowed_min_rtt_.GetThirdBest() + QuicTime::Delta::FromMilliseconds(5);
  // This assert is necessary to avoid triggering -Wstrict-overflow
  // See crbug/616957
  ASSERT_LT(windowed_min_rtt_.GetThirdBest(),
            QuicTime::Delta::Infinite() - QuicTime::Delta::FromMilliseconds(5));
  // Third best min sample was recorded at 100ms, so expiry time is 199ms.
  QuicTime now = QuicTime::Zero() + QuicTime::Delta::FromMilliseconds(200);
  windowed_min_rtt_.Update(rtt_sample, now);
  EXPECT_EQ(rtt_sample, windowed_min_rtt_.GetThirdBest());
  EXPECT_EQ(rtt_sample, windowed_min_rtt_.GetSecondBest());
  EXPECT_EQ(rtt_sample, windowed_min_rtt_.GetBest());
}

TEST_F(WindowedFilterTest, ExpireAllMaxs) {
  InitializeMaxFilter();
  QuicBandwidth bw_sample =
      windowed_max_bw_.GetThirdBest() - QuicBandwidth::FromBitsPerSecond(50);
  // Third best max sample was recorded at 100ms, so expiry time is 199ms.
  QuicTime now = QuicTime::Zero() + QuicTime::Delta::FromMilliseconds(200);
  windowed_max_bw_.Update(bw_sample, now);
  EXPECT_EQ(bw_sample, windowed_max_bw_.GetThirdBest());
  EXPECT_EQ(bw_sample, windowed_max_bw_.GetSecondBest());
  EXPECT_EQ(bw_sample, windowed_max_bw_.GetBest());
}

// Test the windowed filter where the time used is an exact counter instead of a
// timestamp.  This is useful if, for example, the time is measured in round
// trips.
TEST_F(WindowedFilterTest, ExpireCounterBasedMax) {
  // Create a window which starts at t = 0 and expires after two cycles.
  WindowedFilter<uint64_t, MaxFilter<uint64_t>, uint64_t, uint64_t> max_filter(
      2, 0, 0);

  const uint64_t kBest = 50000;
  // Insert 50000 at t = 1.
  max_filter.Update(50000, 1);
  EXPECT_EQ(kBest, max_filter.GetBest());
  UpdateWithIrrelevantSamples(&max_filter, 20, 1);
  EXPECT_EQ(kBest, max_filter.GetBest());

  // Insert 40000 at t = 2.  Nothing is expected to expire.
  max_filter.Update(40000, 2);
  EXPECT_EQ(kBest, max_filter.GetBest());
  UpdateWithIrrelevantSamples(&max_filter, 20, 2);
  EXPECT_EQ(kBest, max_filter.GetBest());

  // Insert 30000 at t = 3.  Nothing is expected to expire yet.
  max_filter.Update(30000, 3);
  EXPECT_EQ(kBest, max_filter.GetBest());
  UpdateWithIrrelevantSamples(&max_filter, 20, 3);
  EXPECT_EQ(kBest, max_filter.GetBest());
  QUIC_VLOG(0) << max_filter.GetSecondBest();
  QUIC_VLOG(0) << max_filter.GetThirdBest();

  // Insert 20000 at t = 4.  50000 at t = 1 expires, so 40000 becomes the new
  // maximum.
  const uint64_t kNewBest = 40000;
  max_filter.Update(20000, 4);
  EXPECT_EQ(kNewBest, max_filter.GetBest());
  UpdateWithIrrelevantSamples(&max_filter, 20, 4);
  EXPECT_EQ(kNewBest, max_filter.GetBest());
}

}  // namespace test
}  // namespace quic
