// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/http/quic_spdy_stream_body_manager.h"

#include <algorithm>
#include <numeric>
#include <string>

#include "net/third_party/quiche/src/quic/platform/api/quic_arraysize.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_expect_bug.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"

namespace quic {

namespace test {

namespace {

class QuicSpdyStreamBodyManagerTest : public QuicTest {
 protected:
  QuicSpdyStreamBodyManager body_manager_;
};

TEST_F(QuicSpdyStreamBodyManagerTest, HasBytesToRead) {
  EXPECT_FALSE(body_manager_.HasBytesToRead());
  EXPECT_EQ(0u, body_manager_.total_body_bytes_received());

  const QuicByteCount header_length = 3;
  EXPECT_EQ(header_length, body_manager_.OnNonBody(header_length));

  EXPECT_FALSE(body_manager_.HasBytesToRead());
  EXPECT_EQ(0u, body_manager_.total_body_bytes_received());

  std::string body(1024, 'a');
  body_manager_.OnBody(body);

  EXPECT_TRUE(body_manager_.HasBytesToRead());
  EXPECT_EQ(1024u, body_manager_.total_body_bytes_received());
}

TEST_F(QuicSpdyStreamBodyManagerTest, ConsumeMoreThanAvailable) {
  std::string body(1024, 'a');
  body_manager_.OnBody(body);
  size_t bytes_to_consume = 0;
  EXPECT_QUIC_BUG(bytes_to_consume = body_manager_.OnBodyConsumed(2048),
                  "Not enough available body to consume.");
  EXPECT_EQ(0u, bytes_to_consume);
}

struct {
  std::vector<QuicByteCount> frame_header_lengths;
  std::vector<const char*> frame_payloads;
  std::vector<QuicByteCount> body_bytes_to_read;
  std::vector<QuicByteCount> expected_return_values;
} const kOnBodyConsumedTestData[] = {
    // One frame consumed in one call.
    {{2}, {"foobar"}, {6}, {6}},
    // Two frames consumed in one call.
    {{3, 5}, {"foobar", "baz"}, {9}, {14}},
    // One frame consumed in two calls.
    {{2}, {"foobar"}, {4, 2}, {4, 2}},
    // Two frames consumed in two calls matching frame boundaries.
    {{3, 5}, {"foobar", "baz"}, {6, 3}, {11, 3}},
    // Two frames consumed in two calls,
    // the first call only consuming part of the first frame.
    {{3, 5}, {"foobar", "baz"}, {5, 4}, {5, 9}},
    // Two frames consumed in two calls,
    // the first call consuming the entire first frame and part of the second.
    {{3, 5}, {"foobar", "baz"}, {7, 2}, {12, 2}},
};

TEST_F(QuicSpdyStreamBodyManagerTest, OnBodyConsumed) {
  for (size_t test_case_index = 0;
       test_case_index < QUIC_ARRAYSIZE(kOnBodyConsumedTestData);
       ++test_case_index) {
    const std::vector<QuicByteCount>& frame_header_lengths =
        kOnBodyConsumedTestData[test_case_index].frame_header_lengths;
    const std::vector<const char*>& frame_payloads =
        kOnBodyConsumedTestData[test_case_index].frame_payloads;
    const std::vector<QuicByteCount>& body_bytes_to_read =
        kOnBodyConsumedTestData[test_case_index].body_bytes_to_read;
    const std::vector<QuicByteCount>& expected_return_values =
        kOnBodyConsumedTestData[test_case_index].expected_return_values;

    for (size_t frame_index = 0; frame_index < frame_header_lengths.size();
         ++frame_index) {
      // Frame header of first frame can immediately be consumed, but not the
      // other frames.  Each test case start with an empty
      // QuicSpdyStreamBodyManager.
      EXPECT_EQ(frame_index == 0 ? frame_header_lengths[frame_index] : 0u,
                body_manager_.OnNonBody(frame_header_lengths[frame_index]));
      body_manager_.OnBody(frame_payloads[frame_index]);
    }

    for (size_t call_index = 0; call_index < body_bytes_to_read.size();
         ++call_index) {
      EXPECT_EQ(expected_return_values[call_index],
                body_manager_.OnBodyConsumed(body_bytes_to_read[call_index]));
    }

    EXPECT_FALSE(body_manager_.HasBytesToRead());
  }
}

struct {
  std::vector<QuicByteCount> frame_header_lengths;
  std::vector<const char*> frame_payloads;
  size_t iov_len;
} const kPeekBodyTestData[] = {
    // No frames, more iovecs than frames.
    {{}, {}, 1},
    // One frame, same number of iovecs.
    {{3}, {"foobar"}, 1},
    // One frame, more iovecs than frames.
    {{3}, {"foobar"}, 2},
    // Two frames, fewer iovecs than frames.
    {{3, 5}, {"foobar", "baz"}, 1},
    // Two frames, same number of iovecs.
    {{3, 5}, {"foobar", "baz"}, 2},
    // Two frames, more iovecs than frames.
    {{3, 5}, {"foobar", "baz"}, 3},
};

TEST_F(QuicSpdyStreamBodyManagerTest, PeekBody) {
  for (size_t test_case_index = 0;
       test_case_index < QUIC_ARRAYSIZE(kPeekBodyTestData); ++test_case_index) {
    const std::vector<QuicByteCount>& frame_header_lengths =
        kPeekBodyTestData[test_case_index].frame_header_lengths;
    const std::vector<const char*>& frame_payloads =
        kPeekBodyTestData[test_case_index].frame_payloads;
    size_t iov_len = kPeekBodyTestData[test_case_index].iov_len;

    QuicSpdyStreamBodyManager body_manager;

    for (size_t frame_index = 0; frame_index < frame_header_lengths.size();
         ++frame_index) {
      // Frame header of first frame can immediately be consumed, but not the
      // other frames.  Each test case uses a new QuicSpdyStreamBodyManager
      // instance.
      EXPECT_EQ(frame_index == 0 ? frame_header_lengths[frame_index] : 0u,
                body_manager.OnNonBody(frame_header_lengths[frame_index]));
      body_manager.OnBody(frame_payloads[frame_index]);
    }

    std::vector<iovec> iovecs;
    iovecs.resize(iov_len);
    size_t iovs_filled = std::min(frame_payloads.size(), iov_len);
    ASSERT_EQ(iovs_filled,
              static_cast<size_t>(body_manager.PeekBody(&iovecs[0], iov_len)));
    for (size_t iovec_index = 0; iovec_index < iovs_filled; ++iovec_index) {
      EXPECT_EQ(frame_payloads[iovec_index],
                QuicStringPiece(
                    static_cast<const char*>(iovecs[iovec_index].iov_base),
                    iovecs[iovec_index].iov_len));
    }
  }
}

struct {
  std::vector<QuicByteCount> frame_header_lengths;
  std::vector<const char*> frame_payloads;
  std::vector<std::vector<QuicByteCount>> iov_lengths;
  std::vector<QuicByteCount> expected_total_bytes_read;
  std::vector<QuicByteCount> expected_return_values;
} const kReadBodyTestData[] = {
    // One frame, one read with smaller iovec.
    {{4}, {"foo"}, {{2}}, {2}, {2}},
    // One frame, one read with same size iovec.
    {{4}, {"foo"}, {{3}}, {3}, {3}},
    // One frame, one read with larger iovec.
    {{4}, {"foo"}, {{5}}, {3}, {3}},
    // One frame, one read with two iovecs, smaller total size.
    {{4}, {"foobar"}, {{2, 3}}, {5}, {5}},
    // One frame, one read with two iovecs, same total size.
    {{4}, {"foobar"}, {{2, 4}}, {6}, {6}},
    // One frame, one read with two iovecs, larger total size in last iovec.
    {{4}, {"foobar"}, {{2, 6}}, {6}, {6}},
    // One frame, one read with extra iovecs, body ends at iovec boundary.
    {{4}, {"foobar"}, {{2, 4, 4, 3}}, {6}, {6}},
    // One frame, one read with extra iovecs, body ends not at iovec boundary.
    {{4}, {"foobar"}, {{2, 7, 4, 3}}, {6}, {6}},
    // One frame, two reads with two iovecs each, smaller total size.
    {{4}, {"foobarbaz"}, {{2, 1}, {3, 2}}, {3, 5}, {3, 5}},
    // One frame, two reads with two iovecs each, same total size.
    {{4}, {"foobarbaz"}, {{2, 1}, {4, 2}}, {3, 6}, {3, 6}},
    // One frame, two reads with two iovecs each, larger total size.
    {{4}, {"foobarbaz"}, {{2, 1}, {4, 10}}, {3, 6}, {3, 6}},
    // Two frames, one read with smaller iovec.
    {{4, 3}, {"foobar", "baz"}, {{8}}, {8}, {11}},
    // Two frames, one read with same size iovec.
    {{4, 3}, {"foobar", "baz"}, {{9}}, {9}, {12}},
    // Two frames, one read with larger iovec.
    {{4, 3}, {"foobar", "baz"}, {{10}}, {9}, {12}},
    // Two frames, one read with two iovecs, smaller total size.
    {{4, 3}, {"foobar", "baz"}, {{4, 3}}, {7}, {10}},
    // Two frames, one read with two iovecs, same total size.
    {{4, 3}, {"foobar", "baz"}, {{4, 5}}, {9}, {12}},
    // Two frames, one read with two iovecs, larger total size in last iovec.
    {{4, 3}, {"foobar", "baz"}, {{4, 6}}, {9}, {12}},
    // Two frames, one read with extra iovecs, body ends at iovec boundary.
    {{4, 3}, {"foobar", "baz"}, {{4, 6, 4, 3}}, {9}, {12}},
    // Two frames, one read with extra iovecs, body ends not at iovec boundary.
    {{4, 3}, {"foobar", "baz"}, {{4, 7, 4, 3}}, {9}, {12}},
    // Two frames, two reads with two iovecs each, reads end on frame boundary.
    {{4, 3}, {"foobar", "baz"}, {{2, 4}, {2, 1}}, {6, 3}, {9, 3}},
    // Three frames, three reads, extra iovecs, no iovec ends on frame boundary.
    {{4, 3, 6},
     {"foobar", "bazquux", "qux"},
     {{4, 3}, {2, 3}, {5, 3}},
     {7, 5, 4},
     {10, 5, 10}},
};

TEST_F(QuicSpdyStreamBodyManagerTest, ReadBody) {
  for (size_t test_case_index = 0;
       test_case_index < QUIC_ARRAYSIZE(kReadBodyTestData); ++test_case_index) {
    const std::vector<QuicByteCount>& frame_header_lengths =
        kReadBodyTestData[test_case_index].frame_header_lengths;
    const std::vector<const char*>& frame_payloads =
        kReadBodyTestData[test_case_index].frame_payloads;
    const std::vector<std::vector<QuicByteCount>>& iov_lengths =
        kReadBodyTestData[test_case_index].iov_lengths;
    const std::vector<QuicByteCount>& expected_total_bytes_read =
        kReadBodyTestData[test_case_index].expected_total_bytes_read;
    const std::vector<QuicByteCount>& expected_return_values =
        kReadBodyTestData[test_case_index].expected_return_values;

    QuicSpdyStreamBodyManager body_manager;

    std::string received_body;

    for (size_t frame_index = 0; frame_index < frame_header_lengths.size();
         ++frame_index) {
      // Frame header of first frame can immediately be consumed, but not the
      // other frames.  Each test case uses a new QuicSpdyStreamBodyManager
      // instance.
      EXPECT_EQ(frame_index == 0 ? frame_header_lengths[frame_index] : 0u,
                body_manager.OnNonBody(frame_header_lengths[frame_index]));
      body_manager.OnBody(frame_payloads[frame_index]);
      received_body.append(frame_payloads[frame_index]);
    }

    std::string read_body;

    for (size_t call_index = 0; call_index < iov_lengths.size(); ++call_index) {
      // Allocate single buffer for iovecs.
      size_t total_iov_length = std::accumulate(iov_lengths[call_index].begin(),
                                                iov_lengths[call_index].end(),
                                                static_cast<size_t>(0));
      std::string buffer(total_iov_length, 'z');

      // Construct iovecs pointing to contiguous areas in the buffer.
      std::vector<iovec> iovecs;
      size_t offset = 0;
      for (size_t iov_length : iov_lengths[call_index]) {
        CHECK(offset + iov_length <= buffer.size());
        iovecs.push_back({&buffer[offset], iov_length});
        offset += iov_length;
      }

      // Make sure |total_bytes_read| differs from |expected_total_bytes_read|.
      size_t total_bytes_read = expected_total_bytes_read[call_index] + 12;
      EXPECT_EQ(
          expected_return_values[call_index],
          body_manager.ReadBody(&iovecs[0], iovecs.size(), &total_bytes_read));
      read_body.append(buffer.substr(0, total_bytes_read));
    }

    EXPECT_EQ(received_body.substr(0, read_body.size()), read_body);
    EXPECT_EQ(read_body.size() < received_body.size(),
              body_manager.HasBytesToRead());
  }
}

}  // anonymous namespace

}  // namespace test

}  // namespace quic
