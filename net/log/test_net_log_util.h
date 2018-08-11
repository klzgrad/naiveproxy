// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_LOG_TEST_NET_LOG_UTIL_H_
#define NET_LOG_TEST_NET_LOG_UTIL_H_

#include <stddef.h>

#include "net/log/net_log_event_type.h"
#include "net/log/test_net_log_entry.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {

// Checks that the element of |entries| at |offset| has the provided values.
// A negative |offset| indicates a position relative to the end of |entries|.
// Checks to make sure |offset| is within bounds, and fails gracefully if it
// isn't.
::testing::AssertionResult LogContainsEvent(
    const TestNetLogEntry::List& entries,
    int offset,
    NetLogEventType expected_event,
    NetLogEventPhase expected_phase);

// Just like LogContainsEvent, but always checks for an EventPhase of
// PHASE_BEGIN.
::testing::AssertionResult LogContainsBeginEvent(
    const TestNetLogEntry::List& entries,
    int offset,
    NetLogEventType expected_event);

// Just like LogContainsEvent, but always checks for an EventPhase of PHASE_END.
::testing::AssertionResult LogContainsEndEvent(
    const TestNetLogEntry::List& entries,
    int offset,
    NetLogEventType expected_event);

// Just like LogContainsEvent, but does not check phase.
::testing::AssertionResult LogContainsEntryWithType(
    const TestNetLogEntry::List& entries,
    int offset,
    NetLogEventType type);

// Check if the log contains an entry of the given type at |start_offset| or
// after.  It is not a failure if there's an earlier matching entry.  Negative
// offsets are relative to the end of the array.
::testing::AssertionResult LogContainsEntryWithTypeAfter(
    const TestNetLogEntry::List& entries,
    int start_offset,
    NetLogEventType type);

// Check if the first entry with the specified values is at |start_offset| or
// after. It is a failure if there's an earlier matching entry.  Negative
// offsets are relative to the end of the array.
size_t ExpectLogContainsSomewhere(const TestNetLogEntry::List& entries,
                                  size_t min_offset,
                                  NetLogEventType expected_event,
                                  NetLogEventPhase expected_phase);

// Check if the log contains an entry with  the given values at |start_offset|
// or after.  It is not a failure if there's an earlier matching entry.
// Negative offsets are relative to the end of the array.
size_t ExpectLogContainsSomewhereAfter(const TestNetLogEntry::List& entries,
                                       size_t start_offset,
                                       NetLogEventType expected_event,
                                       NetLogEventPhase expected_phase);

}  // namespace net

#endif  // NET_LOG_TEST_NET_LOG_UTIL_H_
