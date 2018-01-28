// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/err.h"

#include <stddef.h>

#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "tools/gn/filesystem_utils.h"
#include "tools/gn/input_file.h"
#include "tools/gn/parse_tree.h"
#include "tools/gn/standard_out.h"
#include "tools/gn/tokenizer.h"
#include "tools/gn/value.h"

namespace {

std::string GetNthLine(const base::StringPiece& data, int n) {
  size_t line_off = Tokenizer::ByteOffsetOfNthLine(data, n);
  size_t end = line_off + 1;
  while (end < data.size() && !Tokenizer::IsNewline(data, end))
    end++;
  return data.substr(line_off, end - line_off).as_string();
}

void FillRangeOnLine(const LocationRange& range, int line_number,
                     std::string* line) {
  // Only bother if the range's begin or end overlaps the line. If the entire
  // line is highlighted as a result of this range, it's not very helpful.
  if (range.begin().line_number() != line_number &&
      range.end().line_number() != line_number)
    return;

  // Watch out, the char offsets in the location are 1-based, so we have to
  // subtract 1.
  int begin_char;
  if (range.begin().line_number() < line_number)
    begin_char = 0;
  else
    begin_char = range.begin().column_number() - 1;

  int end_char;
  if (range.end().line_number() > line_number)
    end_char = static_cast<int>(line->size());  // Ending is non-inclusive.
  else
    end_char = range.end().column_number() - 1;

  CHECK(end_char >= begin_char);
  CHECK(begin_char >= 0 && begin_char <= static_cast<int>(line->size()));
  CHECK(end_char >= 0 && end_char <= static_cast<int>(line->size()));
  for (int i = begin_char; i < end_char; i++)
    line->at(i) = '-';
}

// The line length is used to clip the maximum length of the markers we'll
// make if the error spans more than one line (like unterminated literals).
void OutputHighlighedPosition(const Location& location,
                              const Err::RangeList& ranges,
                              size_t line_length) {
  // Make a buffer of the line in spaces.
  std::string highlight;
  highlight.resize(line_length);
  for (size_t i = 0; i < line_length; i++)
    highlight[i] = ' ';

  // Highlight all the ranges on the line.
  for (const auto& range : ranges)
    FillRangeOnLine(range, location.line_number(), &highlight);

  // Allow the marker to be one past the end of the line for marking the end.
  highlight.push_back(' ');
  CHECK(location.column_number() - 1 >= 0 &&
        location.column_number() - 1 < static_cast<int>(highlight.size()));
  highlight[location.column_number() - 1] = '^';

  // Trim unused spaces from end of line.
  while (!highlight.empty() && highlight[highlight.size() - 1] == ' ')
    highlight.resize(highlight.size() - 1);

  highlight += "\n";
  OutputString(highlight, DECORATION_BLUE);
}

}  // namespace

Err::Err() : has_error_(false) {
}

Err::Err(const Location& location,
         const std::string& msg,
         const std::string& help)
    : has_error_(true),
      location_(location),
      message_(msg),
      help_text_(help) {
}

Err::Err(const LocationRange& range,
         const std::string& msg,
         const std::string& help)
    : has_error_(true),
      location_(range.begin()),
      message_(msg),
      help_text_(help) {
  ranges_.push_back(range);
}

Err::Err(const Token& token,
         const std::string& msg,
         const std::string& help)
    : has_error_(true),
      location_(token.location()),
      message_(msg),
      help_text_(help) {
  ranges_.push_back(token.range());
}

Err::Err(const ParseNode* node,
         const std::string& msg,
         const std::string& help_text)
    : has_error_(true),
      message_(msg),
      help_text_(help_text) {
  // Node will be null in certain tests.
  if (node) {
    LocationRange range = node->GetRange();
    location_ = range.begin();
    ranges_.push_back(range);
  }
}

Err::Err(const Value& value,
         const std::string msg,
         const std::string& help_text)
    : has_error_(true),
      message_(msg),
      help_text_(help_text) {
  if (value.origin()) {
    LocationRange range = value.origin()->GetRange();
    location_ = range.begin();
    ranges_.push_back(range);
  }
}

Err::Err(const Err& other) = default;

Err::~Err() {
}

void Err::PrintToStdout() const {
  InternalPrintToStdout(false);
}

void Err::AppendSubErr(const Err& err) {
  sub_errs_.push_back(err);
}

void Err::InternalPrintToStdout(bool is_sub_err) const {
  DCHECK(has_error_);

  if (!is_sub_err)
    OutputString("ERROR ", DECORATION_RED);

  // File name and location.
  const InputFile* input_file = location_.file();
  std::string loc_str = location_.Describe(true);
  if (!loc_str.empty()) {
    if (is_sub_err)
      loc_str.insert(0, "See ");
    else
      loc_str.insert(0, "at ");
    loc_str.append(": ");
  }
  OutputString(loc_str + message_ + "\n");

  // Quoted line.
  if (input_file) {
    std::string line = GetNthLine(input_file->contents(),
                                  location_.line_number());
    if (!base::ContainsOnlyChars(line, base::kWhitespaceASCII)) {
      OutputString(line + "\n", DECORATION_DIM);
      OutputHighlighedPosition(location_, ranges_, line.size());
    }
  }

  // Optional help text.
  if (!help_text_.empty())
    OutputString(help_text_ + "\n");

  // Sub errors.
  for (const auto& sub_err : sub_errs_)
    sub_err.InternalPrintToStdout(true);
}
