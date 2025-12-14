/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "src/trace_processor/types/task_state.h"

#include "test/gtest_and_gmock.h"

namespace perfetto {
namespace trace_processor {
namespace ftrace_utils {
namespace {

TEST(TaskStateUnittest, PrevStateDefaultsToKernelVersion4p4) {
  auto from_raw = [](uint16_t raw) {
    return TaskState::FromRawPrevState(raw, std::nullopt);
  };

  // No kernel version -> default to 4.4
  EXPECT_STREQ(from_raw(0x0).ToString().data(), "R");
  EXPECT_STREQ(from_raw(0x1).ToString().data(), "S");
  EXPECT_STREQ(from_raw(0x2).ToString().data(), "D");
  EXPECT_STREQ(from_raw(0x4).ToString().data(), "T");
  EXPECT_STREQ(from_raw(0x8).ToString().data(), "t");
  EXPECT_STREQ(from_raw(0x10).ToString().data(), "X");
  EXPECT_STREQ(from_raw(0x20).ToString().data(), "Z");

  EXPECT_STREQ(from_raw(0x40).ToString().data(), "x");
  EXPECT_STREQ(from_raw(0x80).ToString().data(), "K");
  EXPECT_STREQ(from_raw(0x100).ToString().data(), "W");
  EXPECT_STREQ(from_raw(0x200).ToString().data(), "P");
  EXPECT_STREQ(from_raw(0x400).ToString().data(), "N");

  EXPECT_STREQ(from_raw(0x800).ToString().data(), "R+");

  // composite states:
  EXPECT_STREQ(from_raw(0x82).ToString().data(), "DK");
  EXPECT_STREQ(from_raw(0x102).ToString().data(), "DW");
}

TEST(TaskStateUnittest, KernelVersion4p8) {
  auto from_raw = [](uint16_t raw) {
    return TaskState::FromRawPrevState(raw, VersionNumber{4, 8});
  };

  // Same as defaults (4.4) except for preempt flag.
  EXPECT_STREQ(from_raw(0x0).ToString().data(), "R");
  EXPECT_STREQ(from_raw(0x1).ToString().data(), "S");
  EXPECT_STREQ(from_raw(0x2).ToString().data(), "D");
  EXPECT_STREQ(from_raw(0x4).ToString().data(), "T");
  EXPECT_STREQ(from_raw(0x8).ToString().data(), "t");
  EXPECT_STREQ(from_raw(0x10).ToString().data(), "X");
  EXPECT_STREQ(from_raw(0x20).ToString().data(), "Z");

  EXPECT_STREQ(from_raw(0x40).ToString().data(), "x");
  EXPECT_STREQ(from_raw(0x80).ToString().data(), "K");
  EXPECT_STREQ(from_raw(0x100).ToString().data(), "W");
  EXPECT_STREQ(from_raw(0x200).ToString().data(), "P");
  EXPECT_STREQ(from_raw(0x400).ToString().data(), "N");

  EXPECT_STREQ(from_raw(0x1000).ToString().data(), "R+");
}

TEST(TaskStateUnittest, KernelVersion4p14) {
  auto from_raw = [](uint16_t raw) {
    return TaskState::FromRawPrevState(raw, VersionNumber{4, 14});
  };

  EXPECT_STREQ(from_raw(0x0).ToString().data(), "R");
  EXPECT_STREQ(from_raw(0x1).ToString().data(), "S");
  EXPECT_STREQ(from_raw(0x2).ToString().data(), "D");
  EXPECT_STREQ(from_raw(0x4).ToString().data(), "T");
  EXPECT_STREQ(from_raw(0x8).ToString().data(), "t");
  EXPECT_STREQ(from_raw(0x10).ToString().data(), "X");
  EXPECT_STREQ(from_raw(0x20).ToString().data(), "Z");

  EXPECT_STREQ(from_raw(0x40).ToString().data(), "P");
  EXPECT_STREQ(from_raw(0x80).ToString().data(), "I");

  EXPECT_STREQ(from_raw(0x100).ToString().data(), "R+");
}

TEST(TaskStateUnittest, PreemptedFlag) {
  // Historical TASK_STATE_MAX as of 4.4:
  {
    TaskState state = TaskState::FromRawPrevState(0x0800, std::nullopt);
    EXPECT_STREQ(state.ToString().data(), "R+");
  }
  // TASK_STATE_MAX moved due to TASK_NEW:
  {
    TaskState state = TaskState::FromRawPrevState(0x1000, VersionNumber{4, 8});
    EXPECT_STREQ(state.ToString().data(), "R+");
  }
  // sched_switch changed to use TASK_REPORT_MAX with one report-specific flag
  // (TASK_REPORT_IDLE):
  {
    TaskState state = TaskState::FromRawPrevState(0x0100, VersionNumber{4, 14});
    EXPECT_STREQ(state.ToString().data(), "R+");
  }
  {
    TaskState state = TaskState::FromRawPrevState(0x0100, VersionNumber{6, 0});
    EXPECT_STREQ(state.ToString().data(), "R+");
  }
}

TEST(TaskStateUnittest, FromParsedFlags) {
  {
    TaskState state =
        TaskState::FromParsedFlags(TaskState::kInterruptibleSleep);
    EXPECT_STREQ(state.ToString().data(), "S");
  }
  {
    TaskState state = TaskState::FromParsedFlags(TaskState::kParked);
    EXPECT_STREQ(state.ToString().data(), "P");
  }
  {
    TaskState state = TaskState::FromParsedFlags(TaskState::kRunnable |
                                                 TaskState::kPreempted);
    EXPECT_STREQ(state.ToString().data(), "R+");
  }
}

// Covers both:
// * parsing from systrace format ("prev_state=D|K")
// * traceconv serializing the "raw" table into systrace format
TEST(TaskStateUnittest, Systrace) {
  auto roundtrip = [](const char* in) {
    uint16_t raw =
        TaskState::FromSystrace(in).ToRawStateOnlyForSystraceConversions();
    return TaskState::FromRawPrevState(raw, std::nullopt).ToString('|');
  };

  EXPECT_STREQ(roundtrip("R").data(), "R");
  EXPECT_STREQ(roundtrip("R+").data(), "R+");
  EXPECT_STREQ(roundtrip("S").data(), "S");
  EXPECT_STREQ(roundtrip("P").data(), "P");
  EXPECT_STREQ(roundtrip("x").data(), "x");
  EXPECT_STREQ(roundtrip("D|K").data(), "D|K");
  EXPECT_STREQ(roundtrip("I").data(), "I");
}

}  // namespace
}  // namespace ftrace_utils
}  // namespace trace_processor
}  // namespace perfetto
