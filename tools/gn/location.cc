// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/location.h"

#include <tuple>

#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "tools/gn/input_file.h"

Location::Location()
    : file_(nullptr),
      line_number_(-1),
      column_number_(-1) {
}

Location::Location(const InputFile* file,
                   int line_number,
                   int column_number,
                   int byte)
    : file_(file),
      line_number_(line_number),
      column_number_(column_number),
      byte_(byte) {
}

bool Location::operator==(const Location& other) const {
  return other.file_ == file_ &&
         other.line_number_ == line_number_ &&
         other.column_number_ == column_number_;
}

bool Location::operator!=(const Location& other) const {
  return !operator==(other);
}

bool Location::operator<(const Location& other) const {
  DCHECK(file_ == other.file_);
  return std::tie(line_number_, column_number_) <
         std::tie(other.line_number_, other.column_number_);
}

std::string Location::Describe(bool include_column_number) const {
  if (!file_)
    return std::string();

  std::string ret;
  if (file_->friendly_name().empty())
    ret = file_->name().value();
  else
    ret = file_->friendly_name();

  ret += ":";
  ret += base::IntToString(line_number_);
  if (include_column_number) {
    ret += ":";
    ret += base::IntToString(column_number_);
  }
  return ret;
}

LocationRange::LocationRange() {
}

LocationRange::LocationRange(const Location& begin, const Location& end)
    : begin_(begin),
      end_(end) {
  DCHECK(begin_.file() == end_.file());
}

LocationRange LocationRange::Union(const LocationRange& other) const {
  DCHECK(begin_.file() == other.begin_.file());
  return LocationRange(
      begin_ < other.begin_ ? begin_ : other.begin_,
      end_ < other.end_ ? other.end_ : end_);
}
