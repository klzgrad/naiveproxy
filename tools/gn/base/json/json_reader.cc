// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/json/json_reader.h"

#include <utility>
#include <vector>

#include "base/json/json_parser.h"
#include "base/logging.h"
#include "base/optional.h"
#include "base/values.h"

namespace base {

// Chosen to support 99.9% of documents found in the wild late 2016.
// http://crbug.com/673263
const int JSONReader::kStackMaxDepth = 200;

// Values 1000 and above are used by JSONFileValueSerializer::JsonFileError.
static_assert(JSONReader::JSON_PARSE_ERROR_COUNT < 1000,
              "JSONReader error out of bounds");

const char JSONReader::kInvalidEscape[] = "Invalid escape sequence.";
const char JSONReader::kSyntaxError[] = "Syntax error.";
const char JSONReader::kUnexpectedToken[] = "Unexpected token.";
const char JSONReader::kTrailingComma[] = "Trailing comma not allowed.";
const char JSONReader::kTooMuchNesting[] = "Too much nesting.";
const char JSONReader::kUnexpectedDataAfterRoot[] =
    "Unexpected data after root element.";
const char JSONReader::kUnsupportedEncoding[] =
    "Unsupported encoding. JSON must be UTF-8.";
const char JSONReader::kUnquotedDictionaryKey[] =
    "Dictionary keys must be quoted.";
const char JSONReader::kInputTooLarge[] = "Input string is too large (>2GB).";

JSONReader::JSONReader(int options, int max_depth)
    : parser_(new internal::JSONParser(options, max_depth)) {}

JSONReader::~JSONReader() = default;

// static
std::unique_ptr<Value> JSONReader::Read(StringPiece json,
                                        int options,
                                        int max_depth) {
  internal::JSONParser parser(options, max_depth);
  Optional<Value> root = parser.Parse(json);
  return root ? std::make_unique<Value>(std::move(*root)) : nullptr;
}

// static
std::unique_ptr<Value> JSONReader::ReadAndReturnError(
    StringPiece json,
    int options,
    int* error_code_out,
    std::string* error_msg_out,
    int* error_line_out,
    int* error_column_out) {
  internal::JSONParser parser(options);
  Optional<Value> root = parser.Parse(json);
  if (!root) {
    if (error_code_out)
      *error_code_out = parser.error_code();
    if (error_msg_out)
      *error_msg_out = parser.GetErrorMessage();
    if (error_line_out)
      *error_line_out = parser.error_line();
    if (error_column_out)
      *error_column_out = parser.error_column();
  }

  return root ? std::make_unique<Value>(std::move(*root)) : nullptr;
}

// static
std::string JSONReader::ErrorCodeToString(JsonParseError error_code) {
  switch (error_code) {
    case JSON_NO_ERROR:
      return std::string();
    case JSON_INVALID_ESCAPE:
      return kInvalidEscape;
    case JSON_SYNTAX_ERROR:
      return kSyntaxError;
    case JSON_UNEXPECTED_TOKEN:
      return kUnexpectedToken;
    case JSON_TRAILING_COMMA:
      return kTrailingComma;
    case JSON_TOO_MUCH_NESTING:
      return kTooMuchNesting;
    case JSON_UNEXPECTED_DATA_AFTER_ROOT:
      return kUnexpectedDataAfterRoot;
    case JSON_UNSUPPORTED_ENCODING:
      return kUnsupportedEncoding;
    case JSON_UNQUOTED_DICTIONARY_KEY:
      return kUnquotedDictionaryKey;
    case JSON_TOO_LARGE:
      return kInputTooLarge;
    case JSON_PARSE_ERROR_COUNT:
      break;
  }
  NOTREACHED();
  return std::string();
}

std::unique_ptr<Value> JSONReader::ReadToValue(StringPiece json) {
  Optional<Value> value = parser_->Parse(json);
  return value ? std::make_unique<Value>(std::move(*value)) : nullptr;
}

JSONReader::JsonParseError JSONReader::error_code() const {
  return parser_->error_code();
}

std::string JSONReader::GetErrorMessage() const {
  return parser_->GetErrorMessage();
}

}  // namespace base
