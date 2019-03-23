// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/i18n/bidi_line_iterator.h"

#include "base/logging.h"

namespace base {
namespace i18n {

namespace {

UBiDiLevel GetParagraphLevelForDirection(TextDirection direction) {
  switch (direction) {
    case UNKNOWN_DIRECTION:
      return UBIDI_DEFAULT_LTR;
      break;
    case RIGHT_TO_LEFT:
      return 1;  // Highest RTL level.
      break;
    case LEFT_TO_RIGHT:
      return 0;  // Highest LTR level.
      break;
    default:
      NOTREACHED();
      return 0;
  }
}

// Overrides the default bidi class for a given character, for the custom
// "AS_URL" behavior. Returns U_BIDI_CLASS_DEFAULT to defer to the default ICU
// behavior.
//
// Matches the C callback interface of ICU's UBiDiClassCallback type (which is
// why there is an unused argument).
UCharDirection GetURLBiDiClassCallback(const void* /*unused*/, UChar32 c) {
  // Note: Use a switch statement instead of strchr() to avoid iterating over a
  // string for each character (the switch allows for much better compiler
  // optimization).
  switch (c) {
    // The set of characters that delimit URL components (separating the scheme,
    // username, password, domain labels, host, path segments, query
    // names/values and fragment).
    case '#':
    case '&':
    case '.':
    case '/':
    case ':':
    case '=':
    case '?':
    case '@':
      // Treat all of these characters as strong LTR, which effectively
      // surrounds all of the text components of a URL (e.g., the domain labels
      // and path segments) in a left-to-right embedding. This ensures that the
      // URL components read from left to right, regardless of any RTL
      // characters. (Within each component, RTL sequences are rendered from
      // right to left as expected.)
      return U_LEFT_TO_RIGHT;
    default:
      return U_BIDI_CLASS_DEFAULT;
  }
}

}  // namespace

BiDiLineIterator::BiDiLineIterator() : bidi_(nullptr) {}

BiDiLineIterator::~BiDiLineIterator() {
  if (bidi_) {
    ubidi_close(bidi_);
    bidi_ = nullptr;
  }
}

bool BiDiLineIterator::Open(const string16& text,
                            TextDirection direction,
                            CustomBehavior behavior) {
  DCHECK(!bidi_);
  UErrorCode error = U_ZERO_ERROR;
  bidi_ = ubidi_openSized(static_cast<int>(text.length()), 0, &error);
  if (U_FAILURE(error))
    return false;

  if (behavior == CustomBehavior::AS_URL) {
    ubidi_setClassCallback(bidi_, GetURLBiDiClassCallback, nullptr, nullptr,
                           nullptr, &error);
    if (U_FAILURE(error))
      return false;
  }

  ubidi_setPara(bidi_, text.data(), static_cast<int>(text.length()),
                GetParagraphLevelForDirection(direction), nullptr, &error);
  return (U_SUCCESS(error));
}

int BiDiLineIterator::CountRuns() const {
  DCHECK(bidi_ != nullptr);
  UErrorCode error = U_ZERO_ERROR;
  const int runs = ubidi_countRuns(bidi_, &error);
  return U_SUCCESS(error) ? runs : 0;
}

UBiDiDirection BiDiLineIterator::GetVisualRun(int index,
                                              int* start,
                                              int* length) const {
  DCHECK(bidi_ != nullptr);
  return ubidi_getVisualRun(bidi_, index, start, length);
}

void BiDiLineIterator::GetLogicalRun(int start,
                                     int* end,
                                     UBiDiLevel* level) const {
  DCHECK(bidi_ != nullptr);
  ubidi_getLogicalRun(bidi_, start, end, level);
}

}  // namespace i18n
}  // namespace base
