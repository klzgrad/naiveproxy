// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include "net/spdy/core/spdy_protocol_test_utils.h"
#include "net/spdy/platform/api/spdy_string_piece.h"

namespace net {
namespace test {

// TODO(jamessynge): Where it makes sense in these functions, it would be nice
// to make use of the existing gMock matchers here, instead of rolling our own.

::testing::AssertionResult VerifySpdyFrameWithHeaderBlockIREquals(
    const SpdyFrameWithHeaderBlockIR& expected,
    const SpdyFrameWithHeaderBlockIR& actual) {
  DVLOG(1) << "VerifySpdyFrameWithHeaderBlockIREquals";
  if (actual.header_block() != expected.header_block())
    return ::testing::AssertionFailure();

  return ::testing::AssertionSuccess();
}

::testing::AssertionResult VerifySpdyFrameIREquals(const SpdyAltSvcIR& expected,
                                                   const SpdyAltSvcIR& actual) {
  if (expected.stream_id() != actual.stream_id())
    return ::testing::AssertionFailure();
  if (expected.origin() != actual.origin())
    return ::testing::AssertionFailure();
  if (actual.altsvc_vector() != expected.altsvc_vector())
    return ::testing::AssertionFailure();

  return ::testing::AssertionSuccess();
}

::testing::AssertionResult VerifySpdyFrameIREquals(
    const SpdyContinuationIR& expected,
    const SpdyContinuationIR& actual) {
  return ::testing::AssertionFailure()
         << "VerifySpdyFrameIREquals SpdyContinuationIR not yet implemented";
}

::testing::AssertionResult VerifySpdyFrameIREquals(const SpdyDataIR& expected,
                                                   const SpdyDataIR& actual) {
  DVLOG(1) << "VerifySpdyFrameIREquals SpdyDataIR";
  if (expected.stream_id() != actual.stream_id())
    return ::testing::AssertionFailure();
  if (expected.fin() != actual.fin())
    return ::testing::AssertionFailure();
  if (expected.data_len() != actual.data_len())
    return ::testing::AssertionFailure();
  if (expected.data() == nullptr && actual.data() != nullptr)
    return ::testing::AssertionFailure();
  if (SpdyStringPiece(expected.data(), expected.data_len()) !=
      SpdyStringPiece(actual.data(), actual.data_len()))
    return ::testing::AssertionFailure();
  if (!VerifySpdyFrameWithPaddingIREquals(expected, actual))
    return ::testing::AssertionFailure();

  return ::testing::AssertionSuccess();
}

::testing::AssertionResult VerifySpdyFrameIREquals(const SpdyGoAwayIR& expected,
                                                   const SpdyGoAwayIR& actual) {
  DVLOG(1) << "VerifySpdyFrameIREquals SpdyGoAwayIR";
  if (expected.last_good_stream_id() != actual.last_good_stream_id())
    return ::testing::AssertionFailure();
  if (expected.error_code() != actual.error_code())
    return ::testing::AssertionFailure();
  if (expected.description() != actual.description())
    return ::testing::AssertionFailure();

  return ::testing::AssertionSuccess();
}

::testing::AssertionResult VerifySpdyFrameIREquals(
    const SpdyHeadersIR& expected,
    const SpdyHeadersIR& actual) {
  DVLOG(1) << "VerifySpdyFrameIREquals SpdyHeadersIR";
  if (expected.stream_id() != actual.stream_id())
    return ::testing::AssertionFailure();
  if (expected.fin() != actual.fin())
    return ::testing::AssertionFailure();
  if (!VerifySpdyFrameWithHeaderBlockIREquals(expected, actual))
    return ::testing::AssertionFailure();
  if (expected.has_priority() != actual.has_priority())
    return ::testing::AssertionFailure();
  if (expected.has_priority()) {
    if (!VerifySpdyFrameWithPriorityIREquals(expected, actual))
      return ::testing::AssertionFailure();
  }
  if (!VerifySpdyFrameWithPaddingIREquals(expected, actual))
    return ::testing::AssertionFailure();

  return ::testing::AssertionSuccess();
}

::testing::AssertionResult VerifySpdyFrameIREquals(const SpdyPingIR& expected,
                                                   const SpdyPingIR& actual) {
  DVLOG(1) << "VerifySpdyFrameIREquals SpdyPingIR";
  if (expected.id() != actual.id())
    return ::testing::AssertionFailure();
  if (expected.is_ack() != actual.is_ack())
    return ::testing::AssertionFailure();

  return ::testing::AssertionSuccess();
}

::testing::AssertionResult VerifySpdyFrameIREquals(
    const SpdyPriorityIR& expected,
    const SpdyPriorityIR& actual) {
  DVLOG(1) << "VerifySpdyFrameIREquals SpdyPriorityIR";
  if (expected.stream_id() != actual.stream_id())
    return ::testing::AssertionFailure();
  if (!VerifySpdyFrameWithPriorityIREquals(expected, actual))
    return ::testing::AssertionFailure();

  return ::testing::AssertionSuccess();
}

::testing::AssertionResult VerifySpdyFrameIREquals(
    const SpdyPushPromiseIR& expected,
    const SpdyPushPromiseIR& actual) {
  DVLOG(1) << "VerifySpdyFrameIREquals SpdyPushPromiseIR";
  if (expected.stream_id() != actual.stream_id())
    return ::testing::AssertionFailure();
  if (!VerifySpdyFrameWithPaddingIREquals(expected, actual))
    return ::testing::AssertionFailure();
  if (expected.promised_stream_id() != actual.promised_stream_id())
    return ::testing::AssertionFailure();
  if (!VerifySpdyFrameWithHeaderBlockIREquals(expected, actual))
    return ::testing::AssertionFailure();

  return ::testing::AssertionSuccess();
}

::testing::AssertionResult VerifySpdyFrameIREquals(
    const SpdyRstStreamIR& expected,
    const SpdyRstStreamIR& actual) {
  DVLOG(1) << "VerifySpdyFrameIREquals SpdyRstStreamIR";
  if (expected.stream_id() != actual.stream_id())
    return ::testing::AssertionFailure();
  if (expected.error_code() != actual.error_code())
    return ::testing::AssertionFailure();

  return ::testing::AssertionSuccess();
}

::testing::AssertionResult VerifySpdyFrameIREquals(
    const SpdySettingsIR& expected,
    const SpdySettingsIR& actual) {
  DVLOG(1) << "VerifySpdyFrameIREquals SpdySettingsIR";
  // Note, ignoring non-HTTP/2 fields such as clear_settings.
  if (expected.is_ack() != actual.is_ack())
    return ::testing::AssertionFailure();

  if (expected.values().size() != actual.values().size())
    return ::testing::AssertionFailure();
  for (const auto& entry : expected.values()) {
    const auto& param = entry.first;
    auto actual_itr = actual.values().find(param);
    if (actual_itr == actual.values().end()) {
      DVLOG(1) << "actual doesn't contain param: " << param;
      return ::testing::AssertionFailure();
    }
    uint32_t expected_value = entry.second;
    uint32_t actual_value = actual_itr->second;
    if (expected_value != actual_value) {
      DVLOG(1) << "Values don't match for parameter: " << param;
      return ::testing::AssertionFailure();
    }
  }

  return ::testing::AssertionSuccess();
}

::testing::AssertionResult VerifySpdyFrameIREquals(
    const SpdyWindowUpdateIR& expected,
    const SpdyWindowUpdateIR& actual) {
  DVLOG(1) << "VerifySpdyFrameIREquals SpdyWindowUpdateIR";
  if (expected.stream_id() != actual.stream_id())
    return ::testing::AssertionFailure();
  if (expected.delta() != actual.delta())
    return ::testing::AssertionFailure();

  return ::testing::AssertionSuccess();
}

}  // namespace test
}  // namespace net
