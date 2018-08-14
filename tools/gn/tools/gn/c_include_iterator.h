// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_C_INCLUDE_ITERATOR_H_
#define TOOLS_GN_C_INCLUDE_ITERATOR_H_

#include <stddef.h>

#include "base/macros.h"
#include "base/strings/string_piece.h"

class InputFile;
class LocationRange;

// Iterates through #includes in C source and header files.
//
// This only returns includes we want to check, which is user includes with
// double-quotes: #include "..."
class CIncludeIterator {
 public:
  // The InputFile pointed to must outlive this class.
  explicit CIncludeIterator(const InputFile* input);
  ~CIncludeIterator();

  // Fills in the string with the contents of the next include, and the
  // location with where it came from, and returns true, or returns false if
  // there are no more includes.
  bool GetNextIncludeString(base::StringPiece* out, LocationRange* location);

  // Maximum numbef of non-includes we'll tolerate before giving up. This does
  // not count comments or preprocessor.
  static const int kMaxNonIncludeLines;

 private:
  // Returns false on EOF, otherwise fills in the given line and the one-based
  // line number into *line_number;
  bool GetNextLine(base::StringPiece* line, int* line_number);

  const InputFile* input_file_;

  // This just points into input_file_.contents() for convenience.
  base::StringPiece file_;

  // 0-based offset into the file.
  size_t offset_;

  int line_number_;  // One-based. Indicates the last line we read.

  // Number of lines we've processed since seeing the last include (or the
  // beginning of the file) with some exceptions.
  int lines_since_last_include_;

  DISALLOW_COPY_AND_ASSIGN(CIncludeIterator);
};

#endif  // TOOLS_GN_C_INCLUDE_ITERATOR_H_
