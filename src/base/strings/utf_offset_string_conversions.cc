// Copyright 2011 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/strings/utf_offset_string_conversions.h"

#include <stdint.h>

#include <algorithm>
#include <memory>
#include <string_view>

#include "base/check_op.h"
#include "base/strings/utf_string_conversion_utils.h"

namespace base {

OffsetAdjuster::Adjustment::Adjustment(size_t original_offset,
                                       size_t original_length,
                                       size_t output_length)
    : original_offset(original_offset),
      original_length(original_length),
      output_length(output_length) {}

// static
void OffsetAdjuster::AdjustOffsets(const Adjustments& adjustments,
                                   std::vector<size_t>* offsets_for_adjustment,
                                   size_t limit) {
  DCHECK(offsets_for_adjustment);
  for (auto& i : *offsets_for_adjustment) {
    AdjustOffset(adjustments, &i, limit);
  }
}

// static
void OffsetAdjuster::AdjustOffset(const Adjustments& adjustments,
                                  size_t* offset,
                                  size_t limit) {
  DCHECK(offset);
  if (*offset == std::u16string::npos) {
    return;
  }
  size_t original_lengths = 0;
  size_t output_lengths = 0;
  for (const auto& i : adjustments) {
    if (*offset <= i.original_offset) {
      break;
    }
    if (*offset < (i.original_offset + i.original_length)) {
      *offset = std::u16string::npos;
      return;
    }
    original_lengths += i.original_length;
    output_lengths += i.output_length;
  }
  *offset += output_lengths - original_lengths;

  if (*offset > limit) {
    *offset = std::u16string::npos;
  }
}

// static
void OffsetAdjuster::UnadjustOffsets(
    const Adjustments& adjustments,
    std::vector<size_t>* offsets_for_unadjustment) {
  if (!offsets_for_unadjustment || adjustments.empty()) {
    return;
  }
  for (auto& i : *offsets_for_unadjustment) {
    UnadjustOffset(adjustments, &i);
  }
}

// static
void OffsetAdjuster::UnadjustOffset(const Adjustments& adjustments,
                                    size_t* offset) {
  if (*offset == std::u16string::npos) {
    return;
  }
  size_t original_lengths = 0;
  size_t output_lengths = 0;
  for (const auto& i : adjustments) {
    if (*offset + original_lengths - output_lengths <= i.original_offset) {
      break;
    }
    original_lengths += i.original_length;
    output_lengths += i.output_length;
    if ((*offset + original_lengths - output_lengths) <
        (i.original_offset + i.original_length)) {
      *offset = std::u16string::npos;
      return;
    }
  }
  *offset += original_lengths - output_lengths;
}

// static
void OffsetAdjuster::MergeSequentialAdjustments(
    const Adjustments& first_adjustments,
    Adjustments* adjustments_on_adjusted_string) {
  auto adjusted_iter = adjustments_on_adjusted_string->begin();
  auto first_iter = first_adjustments.begin();
  // Simultaneously iterate over all |adjustments_on_adjusted_string| and
  // |first_adjustments|, pushing adjustments at the end of
  // |adjustments_builder| as we go.  |shift| keeps track of the current number
  // of characters collapsed by |first_adjustments| up to this point.
  // |currently_collapsing| keeps track of the number of characters collapsed by
  // |first_adjustments| into the current |adjusted_iter|'s length.  These are
  // characters that will change |shift| as soon as we're done processing the
  // current |adjusted_iter|; they are not yet reflected in |shift|.
  size_t shift = 0;
  size_t currently_collapsing = 0;
  // While we *could* update |adjustments_on_adjusted_string| in place by
  // inserting new adjustments into the middle, we would be repeatedly calling
  // |std::vector::insert|. That would cost O(n) time per insert, relative to
  // distance from end of the string.  By instead allocating
  // |adjustments_builder| and calling |std::vector::push_back|, we only pay
  // amortized constant time per push. We are trading space for time.
  Adjustments adjustments_builder;
  while (adjusted_iter != adjustments_on_adjusted_string->end()) {
    if ((first_iter == first_adjustments.end()) ||
        ((adjusted_iter->original_offset + shift +
          adjusted_iter->original_length) <= first_iter->original_offset)) {
      // Entire |adjusted_iter| (accounting for its shift and including its
      // whole original length) comes before |first_iter|.
      //
      // Correct the offset at |adjusted_iter| and move onto the next
      // adjustment that needs revising.
      adjusted_iter->original_offset += shift;
      shift += currently_collapsing;
      currently_collapsing = 0;
      adjustments_builder.push_back(*adjusted_iter);
      ++adjusted_iter;
    } else if ((adjusted_iter->original_offset + shift) >
               first_iter->original_offset) {
      // |first_iter| comes before the |adjusted_iter| (as adjusted by |shift|).

      // It's not possible for the adjustments to overlap.  (It shouldn't
      // be possible that we have an |adjusted_iter->original_offset| that,
      // when adjusted by the computed |shift|, is in the middle of
      // |first_iter|'s output's length.  After all, that would mean the
      // current adjustment_on_adjusted_string somehow points to an offset
      // that was supposed to have been eliminated by the first set of
      // adjustments.)
      DCHECK_LE(first_iter->original_offset + first_iter->output_length,
                adjusted_iter->original_offset + shift);

      // Add the |first_iter| to the full set of adjustments.
      shift += first_iter->original_length - first_iter->output_length;
      adjustments_builder.push_back(*first_iter);
      ++first_iter;
    } else {
      // The first adjustment adjusted something that then got further adjusted
      // by the second set of adjustments.  In other words, |first_iter| points
      // to something in the range covered by |adjusted_iter|'s length (after
      // accounting for |shift|).  Precisely,
      //   adjusted_iter->original_offset + shift
      //   <=
      //   first_iter->original_offset
      //   <=
      //   adjusted_iter->original_offset + shift +
      //       adjusted_iter->original_length
      // Modify the current |adjusted_iter| to include whatever collapsing
      // happened in |first_iter|, then advance to the next |first_adjustments|
      // because we dealt with the current one.

      // This function does not know how to deal with a string that expands and
      // then gets modified, only strings that collapse and then get modified.
      DCHECK_GT(first_iter->original_length, first_iter->output_length);
      const size_t collapse =
          first_iter->original_length - first_iter->output_length;
      adjusted_iter->original_length += collapse;
      currently_collapsing += collapse;
      ++first_iter;
    }
  }
  DCHECK_EQ(0u, currently_collapsing);
  if (first_iter != first_adjustments.end()) {
    // Only first adjustments are left.  These do not need to be modified.
    // (Their offsets are already correct with respect to the original string.)
    // Append them all.
    DCHECK(adjusted_iter == adjustments_on_adjusted_string->end());
    adjustments_builder.insert(adjustments_builder.end(), first_iter,
                               first_adjustments.end());
  }
  *adjustments_on_adjusted_string = std::move(adjustments_builder);
}

// Converts the given source Unicode character type to the given destination
// Unicode character type as a STL string. The given input buffer and size
// determine the source, and the given output STL string will be replaced by
// the result.  If non-NULL, |adjustments| is set to reflect the all the
// alterations to the string that are not one-character-to-one-character.
// It will always be sorted by increasing offset.
template <typename SrcChar, typename DestStdString>
bool ConvertUnicode(const SrcChar* src,
                    size_t src_len,
                    DestStdString* output,
                    OffsetAdjuster::Adjustments* adjustments) {
  if (adjustments) {
    adjustments->clear();
  }
  bool success = true;
  for (size_t i = 0; i < src_len; i++) {
    base_icu::UChar32 code_point;
    size_t original_i = i;
    size_t chars_written = 0;
    if (ReadUnicodeCharacter(src, src_len, &i, &code_point)) {
      chars_written = WriteUnicodeCharacter(code_point, output);
    } else {
      chars_written = WriteUnicodeCharacter(0xFFFD, output);
      success = false;
    }

    // Only bother writing an adjustment if this modification changed the
    // length of this character.
    // NOTE: ReadUnicodeCharacter() adjusts |i| to point _at_ the last
    // character read, not after it (so that incrementing it in the loop
    // increment will place it at the right location), so we need to account
    // for that in determining the amount that was read.
    if (adjustments && ((i - original_i + 1) != chars_written)) {
      adjustments->emplace_back(original_i, i - original_i + 1, chars_written);
    }
  }
  return success;
}

bool UTF8ToUTF16WithAdjustments(
    const char* src,
    size_t src_len,
    std::u16string* output,
    base::OffsetAdjuster::Adjustments* adjustments) {
  PrepareForUTF16Or32Output(src, src_len, output);
  return ConvertUnicode(src, src_len, output, adjustments);
}

std::u16string UTF8ToUTF16WithAdjustments(
    std::string_view utf8,
    base::OffsetAdjuster::Adjustments* adjustments) {
  std::u16string result;
  UTF8ToUTF16WithAdjustments(utf8.data(), utf8.length(), &result, adjustments);
  return result;
}

std::u16string UTF8ToUTF16AndAdjustOffsets(
    std::string_view utf8,
    std::vector<size_t>* offsets_for_adjustment) {
  for (size_t& offset : *offsets_for_adjustment) {
    if (offset > utf8.length()) {
      offset = std::u16string::npos;
    }
  }
  OffsetAdjuster::Adjustments adjustments;
  std::u16string result = UTF8ToUTF16WithAdjustments(utf8, &adjustments);
  OffsetAdjuster::AdjustOffsets(adjustments, offsets_for_adjustment);
  return result;
}

std::string UTF16ToUTF8AndAdjustOffsets(
    std::u16string_view utf16,
    std::vector<size_t>* offsets_for_adjustment) {
  for (size_t& offset : *offsets_for_adjustment) {
    if (offset > utf16.length()) {
      offset = std::u16string::npos;
    }
  }
  std::string result;
  PrepareForUTF8Output(utf16.data(), utf16.length(), &result);
  OffsetAdjuster::Adjustments adjustments;
  ConvertUnicode(utf16.data(), utf16.length(), &result, &adjustments);
  OffsetAdjuster::AdjustOffsets(adjustments, offsets_for_adjustment);
  return result;
}

}  // namespace base
