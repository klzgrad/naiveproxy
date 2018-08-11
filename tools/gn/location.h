// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_LOCATION_H_
#define TOOLS_GN_LOCATION_H_

#include <string>

class InputFile;

// Represents a place in a source file. Used for error reporting.
class Location {
 public:
  Location();
  Location(const InputFile* file, int line_number, int column_number, int byte);

  const InputFile* file() const { return file_; }
  int line_number() const { return line_number_; }
  int column_number() const { return column_number_; }
  int byte() const { return byte_; }
  bool is_null() const { return *this == Location(); }

  bool operator==(const Location& other) const;
  bool operator!=(const Location& other) const;
  bool operator<(const Location& other) const;

  // Returns a string with the file, line, and (optionally) the character
  // offset for this location. If this location is null, returns an empty
  // string.
  std::string Describe(bool include_column_number) const;

 private:
  const InputFile* file_;  // Null when unset.
  int line_number_;        // -1 when unset. 1-based.
  int column_number_;      // -1 when unset. 1-based.
  int byte_;               // Index into the buffer, 0-based.
};

// Represents a range in a source file. Used for error reporting.
// The end is exclusive i.e. [begin, end)
class LocationRange {
 public:
  LocationRange();
  LocationRange(const Location& begin, const Location& end);

  const Location& begin() const { return begin_; }
  const Location& end() const { return end_; }
  bool is_null() const {
    return begin_.is_null();  // No need to check both for the null case.
  }


  LocationRange Union(const LocationRange& other) const;

 private:
  Location begin_;
  Location end_;
};

#endif  // TOOLS_GN_LOCATION_H_
