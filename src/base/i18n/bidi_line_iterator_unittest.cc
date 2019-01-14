// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/i18n/bidi_line_iterator.h"

#include "base/macros.h"
#include "base/strings/utf_string_conversions.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {
namespace i18n {
namespace {

class BiDiLineIteratorTest : public testing::TestWithParam<TextDirection> {
 public:
  BiDiLineIteratorTest() = default;

  BiDiLineIterator* iterator() { return &iterator_; }

 private:
  BiDiLineIterator iterator_;

  DISALLOW_COPY_AND_ASSIGN(BiDiLineIteratorTest);
};

TEST_P(BiDiLineIteratorTest, OnlyLTR) {
  iterator()->Open(UTF8ToUTF16("abc 😁 测试"), GetParam(),
                   BiDiLineIterator::CustomBehavior::NONE);
  ASSERT_EQ(1, iterator()->CountRuns());

  int start, length;
  EXPECT_EQ(UBIDI_LTR, iterator()->GetVisualRun(0, &start, &length));
  EXPECT_EQ(0, start);
  EXPECT_EQ(9, length);

  int end;
  UBiDiLevel level;
  iterator()->GetLogicalRun(0, &end, &level);
  EXPECT_EQ(9, end);
  if (GetParam() == TextDirection::RIGHT_TO_LEFT)
    EXPECT_EQ(2, level);
  else
    EXPECT_EQ(0, level);
}

TEST_P(BiDiLineIteratorTest, OnlyRTL) {
  iterator()->Open(UTF8ToUTF16("מה השעה"), GetParam(),
                   BiDiLineIterator::CustomBehavior::NONE);
  ASSERT_EQ(1, iterator()->CountRuns());

  int start, length;
  EXPECT_EQ(UBIDI_RTL, iterator()->GetVisualRun(0, &start, &length));
  EXPECT_EQ(0, start);
  EXPECT_EQ(7, length);

  int end;
  UBiDiLevel level;
  iterator()->GetLogicalRun(0, &end, &level);
  EXPECT_EQ(7, end);
  EXPECT_EQ(1, level);
}

TEST_P(BiDiLineIteratorTest, Mixed) {
  iterator()->Open(UTF8ToUTF16("אני משתמש ב- Chrome כדפדפן האינטרנט שלי"),
                   GetParam(), BiDiLineIterator::CustomBehavior::NONE);
  ASSERT_EQ(3, iterator()->CountRuns());

  // We'll get completely different results depending on the top-level paragraph
  // direction.
  if (GetParam() == TextDirection::RIGHT_TO_LEFT) {
    // If para direction is RTL, expect the LTR substring "Chrome" to be nested
    // within the surrounding RTL text.
    int start, length;
    EXPECT_EQ(UBIDI_RTL, iterator()->GetVisualRun(0, &start, &length));
    EXPECT_EQ(19, start);
    EXPECT_EQ(20, length);
    EXPECT_EQ(UBIDI_LTR, iterator()->GetVisualRun(1, &start, &length));
    EXPECT_EQ(13, start);
    EXPECT_EQ(6, length);
    EXPECT_EQ(UBIDI_RTL, iterator()->GetVisualRun(2, &start, &length));
    EXPECT_EQ(0, start);
    EXPECT_EQ(13, length);

    int end;
    UBiDiLevel level;
    iterator()->GetLogicalRun(0, &end, &level);
    EXPECT_EQ(13, end);
    EXPECT_EQ(1, level);
    iterator()->GetLogicalRun(13, &end, &level);
    EXPECT_EQ(19, end);
    EXPECT_EQ(2, level);
    iterator()->GetLogicalRun(19, &end, &level);
    EXPECT_EQ(39, end);
    EXPECT_EQ(1, level);
  } else {
    // If the para direction is LTR, expect the LTR substring "- Chrome " to be
    // at the top level, with two nested RTL runs on either side.
    int start, length;
    EXPECT_EQ(UBIDI_RTL, iterator()->GetVisualRun(0, &start, &length));
    EXPECT_EQ(0, start);
    EXPECT_EQ(11, length);
    EXPECT_EQ(UBIDI_LTR, iterator()->GetVisualRun(1, &start, &length));
    EXPECT_EQ(11, start);
    EXPECT_EQ(9, length);
    EXPECT_EQ(UBIDI_RTL, iterator()->GetVisualRun(2, &start, &length));
    EXPECT_EQ(20, start);
    EXPECT_EQ(19, length);

    int end;
    UBiDiLevel level;
    iterator()->GetLogicalRun(0, &end, &level);
    EXPECT_EQ(11, end);
    EXPECT_EQ(1, level);
    iterator()->GetLogicalRun(11, &end, &level);
    EXPECT_EQ(20, end);
    EXPECT_EQ(0, level);
    iterator()->GetLogicalRun(20, &end, &level);
    EXPECT_EQ(39, end);
    EXPECT_EQ(1, level);
  }
}

TEST_P(BiDiLineIteratorTest, RTLPunctuationNoCustomBehavior) {
  // This string features Hebrew characters interleaved with ASCII punctuation.
  iterator()->Open(UTF8ToUTF16("א!ב\"ג#ד$ה%ו&ז'ח(ט)י*ך+כ,ל-ם.מ/"
                               "ן:נ;ס<ע=ף>פ?ץ@צ[ק\\ר]ש^ת_א`ב{ג|ד}ה~ו"),
                   GetParam(), BiDiLineIterator::CustomBehavior::NONE);

  // Expect a single RTL run.
  ASSERT_EQ(1, iterator()->CountRuns());

  int start, length;
  EXPECT_EQ(UBIDI_RTL, iterator()->GetVisualRun(0, &start, &length));
  EXPECT_EQ(0, start);
  EXPECT_EQ(65, length);

  int end;
  UBiDiLevel level;
  iterator()->GetLogicalRun(0, &end, &level);
  EXPECT_EQ(65, end);
  EXPECT_EQ(1, level);
}

TEST_P(BiDiLineIteratorTest, RTLPunctuationAsURL) {
  // This string features Hebrew characters interleaved with ASCII punctuation.
  iterator()->Open(UTF8ToUTF16("א!ב\"ג#ד$ה%ו&ז'ח(ט)י*ך+כ,ל-ם.מ/"
                               "ן:נ;ס<ע=ף>פ?ץ@צ[ק\\ר]ש^ת_א`ב{ג|ד}ה~ו"),
                   GetParam(), BiDiLineIterator::CustomBehavior::AS_URL);

  const int kStringSize = 65;

  // Expect a primary RTL run, broken up by each of the 8 punctuation marks that
  // are considered strong LTR (17 runs total).
  struct {
    int start;
    UBiDiDirection dir;
  } expected_runs[] = {
      {0, UBIDI_RTL},  {5, UBIDI_LTR},   // '#'
      {6, UBIDI_RTL},  {11, UBIDI_LTR},  // '&'
      {12, UBIDI_RTL}, {27, UBIDI_LTR},  // '.'
      {28, UBIDI_RTL}, {29, UBIDI_LTR},  // '/'
      {30, UBIDI_RTL}, {31, UBIDI_LTR},  // ':'
      {32, UBIDI_RTL}, {37, UBIDI_LTR},  // '='
      {38, UBIDI_RTL}, {41, UBIDI_LTR},  // '?'
      {42, UBIDI_RTL}, {43, UBIDI_LTR},  // '@'
      {44, UBIDI_RTL},
  };

  ASSERT_EQ(arraysize(expected_runs),
            static_cast<size_t>(iterator()->CountRuns()));

  for (size_t i = 0; i < arraysize(expected_runs); ++i) {
    const auto& expected_run = expected_runs[i];
    int expected_run_end = i >= arraysize(expected_runs) - 1
                               ? kStringSize
                               : expected_runs[i + 1].start;

    size_t visual_index = GetParam() == TextDirection::RIGHT_TO_LEFT
                              ? arraysize(expected_runs) - 1 - i
                              : i;
    int start, length;
    EXPECT_EQ(expected_run.dir,
              iterator()->GetVisualRun(visual_index, &start, &length))
        << "(i = " << i << ")";
    EXPECT_EQ(expected_run.start, start) << "(i = " << i << ")";
    EXPECT_EQ(expected_run_end - expected_run.start, length)
        << "(i = " << i << ")";

    int expected_level =
        expected_run.dir == UBIDI_RTL
            ? 1
            : (GetParam() == TextDirection::RIGHT_TO_LEFT ? 2 : 0);
    int end;
    UBiDiLevel level;
    iterator()->GetLogicalRun(expected_run.start, &end, &level);
    EXPECT_EQ(expected_run_end, end) << "(i = " << i << ")";
    EXPECT_EQ(expected_level, level) << "(i = " << i << ")";
  }
}

INSTANTIATE_TEST_CASE_P(,
                        BiDiLineIteratorTest,
                        ::testing::Values(TextDirection::LEFT_TO_RIGHT,
                                          TextDirection::RIGHT_TO_LEFT));

}  // namespace
}  // namespace i18n
}  // namespace base
