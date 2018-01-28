// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/c_include_iterator.h"

#include "base/logging.h"
#include "base/macros.h"
#include "base/strings/string_util.h"
#include "tools/gn/input_file.h"
#include "tools/gn/location.h"

namespace {

enum IncludeType {
  INCLUDE_NONE,
  INCLUDE_SYSTEM,  // #include <...>
  INCLUDE_USER     // #include "..."
};

// Returns a new string piece referencing the same buffer as the argument, but
// with leading space trimmed. This only checks for space and tab characters
// since we're dealing with lines in C source files.
base::StringPiece TrimLeadingWhitespace(const base::StringPiece& str) {
  size_t new_begin = 0;
  while (new_begin < str.size() &&
         (str[new_begin] == ' ' || str[new_begin] == '\t'))
    new_begin++;
  return str.substr(new_begin);
}

// We don't want to count comment lines and preprocessor lines toward our
// "max lines to look at before giving up" since the beginnings of some files
// may have a lot of comments.
//
// We only handle C-style "//" comments since this is the normal commenting
// style used in Chrome, and do so pretty stupidly. We don't want to write a
// full C++ parser here, we're just trying to get a good heuristic for checking
// the file.
//
// We assume the line has leading whitespace trimmed. We also assume that empty
// lines have already been filtered out.
bool ShouldCountTowardNonIncludeLines(const base::StringPiece& line) {
  if (base::StartsWith(line, "//", base::CompareCase::SENSITIVE))
    return false;  // Don't count comments.
  if (base::StartsWith(line, "/*", base::CompareCase::SENSITIVE) ||
      base::StartsWith(line, " *", base::CompareCase::SENSITIVE))
    return false;  // C-style comment blocks with stars along the left side.
  if (base::StartsWith(line, "#", base::CompareCase::SENSITIVE))
    return false;  // Don't count preprocessor.
  if (base::ContainsOnlyChars(line, base::kWhitespaceASCII))
    return false;  // Don't count whitespace lines.
  return true;  // Count everything else.
}

// Given a line, checks to see if it looks like an include or import and
// extract the path. The type of include is returned. Returns INCLUDE_NONE on
// error or if this is not an include line.
//
// The 1-based character number on the line that the include was found at
// will be filled into *begin_char.
IncludeType ExtractInclude(const base::StringPiece& line,
                           base::StringPiece* path,
                           int* begin_char) {
  static const char kInclude[] = "#include";
  static const size_t kIncludeLen = arraysize(kInclude) - 1;  // No null.
  static const char kImport[] = "#import";
  static const size_t kImportLen = arraysize(kImport) - 1;  // No null.

  base::StringPiece trimmed = TrimLeadingWhitespace(line);
  if (trimmed.empty())
    return INCLUDE_NONE;

  base::StringPiece contents;
  if (base::StartsWith(trimmed, base::StringPiece(kInclude, kIncludeLen),
                       base::CompareCase::SENSITIVE))
    contents = TrimLeadingWhitespace(trimmed.substr(kIncludeLen));
  else if (base::StartsWith(trimmed, base::StringPiece(kImport, kImportLen),
                            base::CompareCase::SENSITIVE))
    contents = TrimLeadingWhitespace(trimmed.substr(kImportLen));

  if (contents.empty())
    return INCLUDE_NONE;

  IncludeType type = INCLUDE_NONE;
  char terminating_char = 0;
  if (contents[0] == '"') {
    type = INCLUDE_USER;
    terminating_char = '"';
  } else if (contents[0] == '<') {
    type = INCLUDE_SYSTEM;
    terminating_char = '>';
  } else {
    return INCLUDE_NONE;
  }

  // Count everything to next "/> as the contents.
  size_t terminator_index = contents.find(terminating_char, 1);
  if (terminator_index == base::StringPiece::npos)
    return INCLUDE_NONE;

  *path = contents.substr(1, terminator_index - 1);
  // Note: one based so we do "+ 1".
  *begin_char = static_cast<int>(path->data() - line.data()) + 1;
  return type;
}

// Returns true if this line has a "nogncheck" comment associated with it.
bool HasNoCheckAnnotation(const base::StringPiece& line) {
  return line.find("nogncheck") != base::StringPiece::npos;
}

}  // namespace

const int CIncludeIterator::kMaxNonIncludeLines = 10;

CIncludeIterator::CIncludeIterator(const InputFile* input)
    : input_file_(input),
      file_(input->contents()),
      offset_(0),
      line_number_(0),
      lines_since_last_include_(0) {
}

CIncludeIterator::~CIncludeIterator() {
}

bool CIncludeIterator::GetNextIncludeString(base::StringPiece* out,
                                            LocationRange* location) {
  base::StringPiece line;
  int cur_line_number = 0;
  while (lines_since_last_include_ <= kMaxNonIncludeLines &&
         GetNextLine(&line, &cur_line_number)) {
    base::StringPiece include_contents;
    int begin_char;
    IncludeType type = ExtractInclude(line, &include_contents, &begin_char);
    if (type == INCLUDE_USER && !HasNoCheckAnnotation(line)) {
      // Only count user includes for now.
      *out = include_contents;
      *location = LocationRange(
          Location(input_file_,
                   cur_line_number,
                   begin_char,
                   -1 /* TODO(scottmg): Is this important? */),
          Location(input_file_,
                   cur_line_number,
                   begin_char + static_cast<int>(include_contents.size()),
                   -1 /* TODO(scottmg): Is this important? */));

      lines_since_last_include_ = 0;
      return true;
    }

    if (ShouldCountTowardNonIncludeLines(line))
      lines_since_last_include_++;
  }
  return false;
}

bool CIncludeIterator::GetNextLine(base::StringPiece* line, int* line_number) {
  if (offset_ == file_.size())
    return false;

  size_t begin = offset_;
  while (offset_ < file_.size() && file_[offset_] != '\n')
    offset_++;
  line_number_++;

  *line = file_.substr(begin, offset_ - begin);
  *line_number = line_number_;

  // If we didn't hit EOF, skip past the newline for the next one.
  if (offset_ < file_.size())
    offset_++;
  return true;
}
