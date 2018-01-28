// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/json/json_parser.h"

#include <cmath>
#include <utility>

#include "base/logging.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_piece.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversion_utils.h"
#include "base/strings/utf_string_conversions.h"
#include "base/third_party/icu/icu_utf.h"
#include "base/values.h"

namespace base {
namespace internal {

namespace {

// Chosen to support 99.9% of documents found in the wild late 2016.
// http://crbug.com/673263
const int kStackMaxDepth = 200;

const int32_t kExtendedASCIIStart = 0x80;

// Simple class that checks for maximum recursion/"stack overflow."
class StackMarker {
 public:
  explicit StackMarker(int* depth) : depth_(depth) {
    ++(*depth_);
    DCHECK_LE(*depth_, kStackMaxDepth);
  }
  ~StackMarker() {
    --(*depth_);
  }

  bool IsTooDeep() const {
    return *depth_ >= kStackMaxDepth;
  }

 private:
  int* const depth_;

  DISALLOW_COPY_AND_ASSIGN(StackMarker);
};

}  // namespace

// This is U+FFFD.
const char kUnicodeReplacementString[] = "\xEF\xBF\xBD";

JSONParser::JSONParser(int options)
    : options_(options),
      start_pos_(nullptr),
      pos_(nullptr),
      end_pos_(nullptr),
      index_(0),
      stack_depth_(0),
      line_number_(0),
      index_last_line_(0),
      error_code_(JSONReader::JSON_NO_ERROR),
      error_line_(0),
      error_column_(0) {
}

JSONParser::~JSONParser() {
}

std::unique_ptr<Value> JSONParser::Parse(StringPiece input) {
  start_pos_ = input.data();
  pos_ = start_pos_;
  end_pos_ = start_pos_ + input.length();
  index_ = 0;
  line_number_ = 1;
  index_last_line_ = 0;

  error_code_ = JSONReader::JSON_NO_ERROR;
  error_line_ = 0;
  error_column_ = 0;

  // When the input JSON string starts with a UTF-8 Byte-Order-Mark
  // <0xEF 0xBB 0xBF>, advance the start position to avoid the
  // ParseNextToken function mis-treating a Unicode BOM as an invalid
  // character and returning NULL.
  if (CanConsume(3) && static_cast<uint8_t>(*pos_) == 0xEF &&
      static_cast<uint8_t>(*(pos_ + 1)) == 0xBB &&
      static_cast<uint8_t>(*(pos_ + 2)) == 0xBF) {
    NextNChars(3);
  }

  // Parse the first and any nested tokens.
  std::unique_ptr<Value> root(ParseNextToken());
  if (!root)
    return nullptr;

  // Make sure the input stream is at an end.
  if (GetNextToken() != T_END_OF_INPUT) {
    if (!CanConsume(1) || (NextChar() && GetNextToken() != T_END_OF_INPUT)) {
      ReportError(JSONReader::JSON_UNEXPECTED_DATA_AFTER_ROOT, 1);
      return nullptr;
    }
  }

  return root;
}

JSONReader::JsonParseError JSONParser::error_code() const {
  return error_code_;
}

std::string JSONParser::GetErrorMessage() const {
  return FormatErrorMessage(error_line_, error_column_,
      JSONReader::ErrorCodeToString(error_code_));
}

int JSONParser::error_line() const {
  return error_line_;
}

int JSONParser::error_column() const {
  return error_column_;
}

// StringBuilder ///////////////////////////////////////////////////////////////

JSONParser::StringBuilder::StringBuilder() : StringBuilder(nullptr) {}

JSONParser::StringBuilder::StringBuilder(const char* pos)
    : pos_(pos), length_(0) {}

JSONParser::StringBuilder::~StringBuilder() {
}

JSONParser::StringBuilder& JSONParser::StringBuilder::operator=(
    StringBuilder&& other) = default;

void JSONParser::StringBuilder::Append(const char& c) {
  DCHECK_GE(c, 0);
  DCHECK_LT(static_cast<unsigned char>(c), 128);

  if (string_)
    string_->push_back(c);
  else
    ++length_;
}

void JSONParser::StringBuilder::AppendString(const char* str, size_t len) {
  DCHECK(string_);
  string_->append(str, len);
}

void JSONParser::StringBuilder::Convert() {
  if (string_)
    return;

  string_.emplace(pos_, length_);
}

StringPiece JSONParser::StringBuilder::AsStringPiece() {
  if (string_)
    return *string_;
  return StringPiece(pos_, length_);
}

const std::string& JSONParser::StringBuilder::AsString() {
  if (!string_)
    Convert();
  return *string_;
}

std::string JSONParser::StringBuilder::DestructiveAsString() {
  if (string_)
    return std::move(*string_);
  return std::string(pos_, length_);
}

// JSONParser private //////////////////////////////////////////////////////////

inline bool JSONParser::CanConsume(int length) {
  return pos_ + length <= end_pos_;
}

const char* JSONParser::NextChar() {
  DCHECK(CanConsume(1));
  ++index_;
  ++pos_;
  return pos_;
}

void JSONParser::NextNChars(int n) {
  DCHECK(CanConsume(n));
  index_ += n;
  pos_ += n;
}

JSONParser::Token JSONParser::GetNextToken() {
  EatWhitespaceAndComments();
  if (!CanConsume(1))
    return T_END_OF_INPUT;

  switch (*pos_) {
    case '{':
      return T_OBJECT_BEGIN;
    case '}':
      return T_OBJECT_END;
    case '[':
      return T_ARRAY_BEGIN;
    case ']':
      return T_ARRAY_END;
    case '"':
      return T_STRING;
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
    case '-':
      return T_NUMBER;
    case 't':
      return T_BOOL_TRUE;
    case 'f':
      return T_BOOL_FALSE;
    case 'n':
      return T_NULL;
    case ',':
      return T_LIST_SEPARATOR;
    case ':':
      return T_OBJECT_PAIR_SEPARATOR;
    default:
      return T_INVALID_TOKEN;
  }
}

void JSONParser::EatWhitespaceAndComments() {
  while (pos_ < end_pos_) {
    switch (*pos_) {
      case '\r':
      case '\n':
        index_last_line_ = index_;
        // Don't increment line_number_ twice for "\r\n".
        if (!(*pos_ == '\n' && pos_ > start_pos_ && *(pos_ - 1) == '\r'))
          ++line_number_;
        // Fall through.
      case ' ':
      case '\t':
        NextChar();
        break;
      case '/':
        if (!EatComment())
          return;
        break;
      default:
        return;
    }
  }
}

bool JSONParser::EatComment() {
  if (*pos_ != '/' || !CanConsume(1))
    return false;

  NextChar();

  if (!CanConsume(1))
    return false;

  if (*pos_ == '/') {
    // Single line comment, read to newline.
    while (CanConsume(1)) {
      if (*pos_ == '\n' || *pos_ == '\r')
        return true;
      NextChar();
    }
  } else if (*pos_ == '*') {
    char previous_char = '\0';
    // Block comment, read until end marker.
    while (CanConsume(1)) {
      if (previous_char == '*' && *pos_ == '/') {
        // EatWhitespaceAndComments will inspect pos_, which will still be on
        // the last / of the comment, so advance once more (which may also be
        // end of input).
        NextChar();
        return true;
      }
      previous_char = *pos_;
      NextChar();
    }

    // If the comment is unterminated, GetNextToken will report T_END_OF_INPUT.
  }

  return false;
}

std::unique_ptr<Value> JSONParser::ParseNextToken() {
  return ParseToken(GetNextToken());
}

std::unique_ptr<Value> JSONParser::ParseToken(Token token) {
  switch (token) {
    case T_OBJECT_BEGIN:
      return ConsumeDictionary();
    case T_ARRAY_BEGIN:
      return ConsumeList();
    case T_STRING:
      return ConsumeString();
    case T_NUMBER:
      return ConsumeNumber();
    case T_BOOL_TRUE:
    case T_BOOL_FALSE:
    case T_NULL:
      return ConsumeLiteral();
    default:
      ReportError(JSONReader::JSON_UNEXPECTED_TOKEN, 1);
      return nullptr;
  }
}

std::unique_ptr<Value> JSONParser::ConsumeDictionary() {
  if (*pos_ != '{') {
    ReportError(JSONReader::JSON_UNEXPECTED_TOKEN, 1);
    return nullptr;
  }

  StackMarker depth_check(&stack_depth_);
  if (depth_check.IsTooDeep()) {
    ReportError(JSONReader::JSON_TOO_MUCH_NESTING, 1);
    return nullptr;
  }

  std::vector<Value::DictStorage::value_type> dict_storage;

  NextChar();
  Token token = GetNextToken();
  while (token != T_OBJECT_END) {
    if (token != T_STRING) {
      ReportError(JSONReader::JSON_UNQUOTED_DICTIONARY_KEY, 1);
      return nullptr;
    }

    // First consume the key.
    StringBuilder key;
    if (!ConsumeStringRaw(&key)) {
      return nullptr;
    }

    // Read the separator.
    NextChar();
    token = GetNextToken();
    if (token != T_OBJECT_PAIR_SEPARATOR) {
      ReportError(JSONReader::JSON_SYNTAX_ERROR, 1);
      return nullptr;
    }

    // The next token is the value. Ownership transfers to |dict|.
    NextChar();
    std::unique_ptr<Value> value = ParseNextToken();
    if (!value) {
      // ReportError from deeper level.
      return nullptr;
    }

    dict_storage.emplace_back(key.DestructiveAsString(), std::move(value));

    NextChar();
    token = GetNextToken();
    if (token == T_LIST_SEPARATOR) {
      NextChar();
      token = GetNextToken();
      if (token == T_OBJECT_END && !(options_ & JSON_ALLOW_TRAILING_COMMAS)) {
        ReportError(JSONReader::JSON_TRAILING_COMMA, 1);
        return nullptr;
      }
    } else if (token != T_OBJECT_END) {
      ReportError(JSONReader::JSON_SYNTAX_ERROR, 0);
      return nullptr;
    }
  }

  return std::make_unique<Value>(
      Value::DictStorage(std::move(dict_storage), KEEP_LAST_OF_DUPES));
}

std::unique_ptr<Value> JSONParser::ConsumeList() {
  if (*pos_ != '[') {
    ReportError(JSONReader::JSON_UNEXPECTED_TOKEN, 1);
    return nullptr;
  }

  StackMarker depth_check(&stack_depth_);
  if (depth_check.IsTooDeep()) {
    ReportError(JSONReader::JSON_TOO_MUCH_NESTING, 1);
    return nullptr;
  }

  std::unique_ptr<ListValue> list(new ListValue);

  NextChar();
  Token token = GetNextToken();
  while (token != T_ARRAY_END) {
    std::unique_ptr<Value> item = ParseToken(token);
    if (!item) {
      // ReportError from deeper level.
      return nullptr;
    }

    list->Append(std::move(item));

    NextChar();
    token = GetNextToken();
    if (token == T_LIST_SEPARATOR) {
      NextChar();
      token = GetNextToken();
      if (token == T_ARRAY_END && !(options_ & JSON_ALLOW_TRAILING_COMMAS)) {
        ReportError(JSONReader::JSON_TRAILING_COMMA, 1);
        return nullptr;
      }
    } else if (token != T_ARRAY_END) {
      ReportError(JSONReader::JSON_SYNTAX_ERROR, 1);
      return nullptr;
    }
  }

  return std::move(list);
}

std::unique_ptr<Value> JSONParser::ConsumeString() {
  StringBuilder string;
  if (!ConsumeStringRaw(&string))
    return nullptr;

  return std::make_unique<Value>(string.DestructiveAsString());
}

bool JSONParser::ConsumeStringRaw(StringBuilder* out) {
  if (*pos_ != '"') {
    ReportError(JSONReader::JSON_UNEXPECTED_TOKEN, 1);
    return false;
  }

  // Strings are at minimum two characters: the surrounding double quotes.
  if (!CanConsume(2)) {
    ReportError(JSONReader::JSON_SYNTAX_ERROR, 1);
    return false;
  }

  // StringBuilder will internally build a StringPiece unless a UTF-16
  // conversion occurs, at which point it will perform a copy into a
  // std::string.
  StringBuilder string(NextChar());

  // Handle the empty string case early.
  if (*pos_ == '"') {
    *out = std::move(string);
    return true;
  }

  int length = end_pos_ - start_pos_;
  int32_t next_char = 0;

  // There must always be at least two characters left in the stream: the next
  // string character and the terminating closing quote.
  while (CanConsume(2)) {
    int start_index = index_;
    pos_ = start_pos_ + index_;  // CBU8_NEXT is postcrement.
    CBU8_NEXT(start_pos_, index_, length, next_char);
    if (next_char < 0 || !IsValidCharacter(next_char)) {
      if ((options_ & JSON_REPLACE_INVALID_CHARACTERS) == 0) {
        ReportError(JSONReader::JSON_UNSUPPORTED_ENCODING, 1);
        return false;
      }
      CBU8_NEXT(start_pos_, start_index, length, next_char);
      string.Convert();
      string.AppendString(kUnicodeReplacementString,
                          arraysize(kUnicodeReplacementString) - 1);
      continue;
    }

    if (next_char == '"') {
      --index_;  // Rewind by one because of CBU8_NEXT.
      *out = std::move(string);
      return true;
    }

    // If this character is not an escape sequence...
    if (next_char != '\\') {
      if (next_char < kExtendedASCIIStart)
        string.Append(static_cast<char>(next_char));
      else
        DecodeUTF8(next_char, &string);
    } else {
      // And if it is an escape sequence, the input string will be adjusted
      // (either by combining the two characters of an encoded escape sequence,
      // or with a UTF conversion), so using StringPiece isn't possible -- force
      // a conversion.
      string.Convert();

      if (!CanConsume(1)) {
        ReportError(JSONReader::JSON_INVALID_ESCAPE, 0);
        return false;
      }

      NextChar();
      if (!CanConsume(1)) {
        ReportError(JSONReader::JSON_INVALID_ESCAPE, 0);
        return false;
      }

      switch (*pos_) {
        // Allowed esape sequences:
        case 'x': {  // UTF-8 sequence.
          // UTF-8 \x escape sequences are not allowed in the spec, but they
          // are supported here for backwards-compatiblity with the old parser.
          if (!CanConsume(3)) {
            ReportError(JSONReader::JSON_INVALID_ESCAPE, 1);
            return false;
          }

          int hex_digit = 0;
          if (!HexStringToInt(StringPiece(NextChar(), 2), &hex_digit) ||
              !IsValidCharacter(hex_digit)) {
            ReportError(JSONReader::JSON_INVALID_ESCAPE, -1);
            return false;
          }
          NextChar();

          if (hex_digit < kExtendedASCIIStart)
            string.Append(static_cast<char>(hex_digit));
          else
            DecodeUTF8(hex_digit, &string);
          break;
        }
        case 'u': {  // UTF-16 sequence.
          // UTF units are of the form \uXXXX.
          if (!CanConsume(5)) {  // 5 being 'u' and four HEX digits.
            ReportError(JSONReader::JSON_INVALID_ESCAPE, 0);
            return false;
          }

          // Skip the 'u'.
          NextChar();

          std::string utf8_units;
          if (!DecodeUTF16(&utf8_units)) {
            ReportError(JSONReader::JSON_INVALID_ESCAPE, -1);
            return false;
          }

          string.AppendString(utf8_units.data(), utf8_units.length());
          break;
        }
        case '"':
          string.Append('"');
          break;
        case '\\':
          string.Append('\\');
          break;
        case '/':
          string.Append('/');
          break;
        case 'b':
          string.Append('\b');
          break;
        case 'f':
          string.Append('\f');
          break;
        case 'n':
          string.Append('\n');
          break;
        case 'r':
          string.Append('\r');
          break;
        case 't':
          string.Append('\t');
          break;
        case 'v':  // Not listed as valid escape sequence in the RFC.
          string.Append('\v');
          break;
        // All other escape squences are illegal.
        default:
          ReportError(JSONReader::JSON_INVALID_ESCAPE, 0);
          return false;
      }
    }
  }

  ReportError(JSONReader::JSON_SYNTAX_ERROR, 0);
  return false;
}

// Entry is at the first X in \uXXXX.
bool JSONParser::DecodeUTF16(std::string* dest_string) {
  if (!CanConsume(4))
    return false;

  // This is a 32-bit field because the shift operations in the
  // conversion process below cause MSVC to error about "data loss."
  // This only stores UTF-16 code units, though.
  // Consume the UTF-16 code unit, which may be a high surrogate.
  int code_unit16_high = 0;
  if (!HexStringToInt(StringPiece(pos_, 4), &code_unit16_high))
    return false;

  // Only add 3, not 4, because at the end of this iteration, the parser has
  // finished working with the last digit of the UTF sequence, meaning that
  // the next iteration will advance to the next byte.
  NextNChars(3);

  // Used to convert the UTF-16 code units to a code point and then to a UTF-8
  // code unit sequence.
  char code_unit8[8] = { 0 };
  size_t offset = 0;

  // If this is a high surrogate, consume the next code unit to get the
  // low surrogate.
  if (CBU16_IS_SURROGATE(code_unit16_high)) {
    // Make sure this is the high surrogate. If not, it's an encoding
    // error.
    if (!CBU16_IS_SURROGATE_LEAD(code_unit16_high))
      return false;

    // Make sure that the token has more characters to consume the
    // lower surrogate.
    if (!CanConsume(6))  // 6 being '\' 'u' and four HEX digits.
      return false;
    if (*NextChar() != '\\' || *NextChar() != 'u')
      return false;

    NextChar();  // Read past 'u'.
    int code_unit16_low = 0;
    if (!HexStringToInt(StringPiece(pos_, 4), &code_unit16_low))
      return false;

    NextNChars(3);

    if (!CBU16_IS_TRAIL(code_unit16_low)) {
      return false;
    }

    uint32_t code_point =
        CBU16_GET_SUPPLEMENTARY(code_unit16_high, code_unit16_low);
    if (!IsValidCharacter(code_point))
      return false;

    offset = 0;
    CBU8_APPEND_UNSAFE(code_unit8, offset, code_point);
  } else {
    // Not a surrogate.
    DCHECK(CBU16_IS_SINGLE(code_unit16_high));
    if (!IsValidCharacter(code_unit16_high)) {
      if ((options_ & JSON_REPLACE_INVALID_CHARACTERS) == 0) {
        return false;
      }
      dest_string->append(kUnicodeReplacementString);
      return true;
    }

    CBU8_APPEND_UNSAFE(code_unit8, offset, code_unit16_high);
  }

  dest_string->append(code_unit8, offset);
  return true;
}

void JSONParser::DecodeUTF8(const int32_t& point, StringBuilder* dest) {
  DCHECK(IsValidCharacter(point));

  // Anything outside of the basic ASCII plane will need to be decoded from
  // int32_t to a multi-byte sequence.
  if (point < kExtendedASCIIStart) {
    dest->Append(static_cast<char>(point));
  } else {
    char utf8_units[4] = { 0 };
    int offset = 0;
    CBU8_APPEND_UNSAFE(utf8_units, offset, point);
    dest->Convert();
    // CBU8_APPEND_UNSAFE can overwrite up to 4 bytes, so utf8_units may not be
    // zero terminated at this point.  |offset| contains the correct length.
    dest->AppendString(utf8_units, offset);
  }
}

std::unique_ptr<Value> JSONParser::ConsumeNumber() {
  const char* num_start = pos_;
  const int start_index = index_;
  int end_index = start_index;

  if (*pos_ == '-')
    NextChar();

  if (!ReadInt(false)) {
    ReportError(JSONReader::JSON_SYNTAX_ERROR, 1);
    return nullptr;
  }
  end_index = index_;

  // The optional fraction part.
  if (CanConsume(1) && *pos_ == '.') {
    NextChar();
    if (!ReadInt(true)) {
      ReportError(JSONReader::JSON_SYNTAX_ERROR, 1);
      return nullptr;
    }
    end_index = index_;
  }

  // Optional exponent part.
  if (CanConsume(1) && (*pos_ == 'e' || *pos_ == 'E')) {
    NextChar();
    if (!CanConsume(1)) {
      ReportError(JSONReader::JSON_SYNTAX_ERROR, 1);
      return nullptr;
    }
    if (*pos_ == '-' || *pos_ == '+') {
      NextChar();
    }
    if (!ReadInt(true)) {
      ReportError(JSONReader::JSON_SYNTAX_ERROR, 1);
      return nullptr;
    }
    end_index = index_;
  }

  // ReadInt is greedy because numbers have no easily detectable sentinel,
  // so save off where the parser should be on exit (see Consume invariant at
  // the top of the header), then make sure the next token is one which is
  // valid.
  const char* exit_pos = pos_ - 1;
  int exit_index = index_ - 1;

  switch (GetNextToken()) {
    case T_OBJECT_END:
    case T_ARRAY_END:
    case T_LIST_SEPARATOR:
    case T_END_OF_INPUT:
      break;
    default:
      ReportError(JSONReader::JSON_SYNTAX_ERROR, 1);
      return nullptr;
  }

  pos_ = exit_pos;
  index_ = exit_index;

  StringPiece num_string(num_start, end_index - start_index);

  int num_int;
  if (StringToInt(num_string, &num_int))
    return std::make_unique<Value>(num_int);

  double num_double;
  if (StringToDouble(num_string.as_string(), &num_double) &&
      std::isfinite(num_double)) {
    return std::make_unique<Value>(num_double);
  }

  return nullptr;
}

bool JSONParser::ReadInt(bool allow_leading_zeros) {
  size_t len = 0;
  char first = 0;

  while (CanConsume(1)) {
    if (!IsAsciiDigit(*pos_))
      break;

    if (len == 0)
      first = *pos_;

    ++len;
    NextChar();
  }

  if (len == 0)
    return false;

  if (!allow_leading_zeros && len > 1 && first == '0')
    return false;

  return true;
}

std::unique_ptr<Value> JSONParser::ConsumeLiteral() {
  switch (*pos_) {
    case 't': {
      const char kTrueLiteral[] = "true";
      const int kTrueLen = static_cast<int>(strlen(kTrueLiteral));
      if (!CanConsume(kTrueLen) ||
          !StringsAreEqual(pos_, kTrueLiteral, kTrueLen)) {
        ReportError(JSONReader::JSON_SYNTAX_ERROR, 1);
        return nullptr;
      }
      NextNChars(kTrueLen - 1);
      return std::make_unique<Value>(true);
    }
    case 'f': {
      const char kFalseLiteral[] = "false";
      const int kFalseLen = static_cast<int>(strlen(kFalseLiteral));
      if (!CanConsume(kFalseLen) ||
          !StringsAreEqual(pos_, kFalseLiteral, kFalseLen)) {
        ReportError(JSONReader::JSON_SYNTAX_ERROR, 1);
        return nullptr;
      }
      NextNChars(kFalseLen - 1);
      return std::make_unique<Value>(false);
    }
    case 'n': {
      const char kNullLiteral[] = "null";
      const int kNullLen = static_cast<int>(strlen(kNullLiteral));
      if (!CanConsume(kNullLen) ||
          !StringsAreEqual(pos_, kNullLiteral, kNullLen)) {
        ReportError(JSONReader::JSON_SYNTAX_ERROR, 1);
        return nullptr;
      }
      NextNChars(kNullLen - 1);
      return std::make_unique<Value>();
    }
    default:
      ReportError(JSONReader::JSON_UNEXPECTED_TOKEN, 1);
      return nullptr;
  }
}

// static
bool JSONParser::StringsAreEqual(const char* one, const char* two, size_t len) {
  return strncmp(one, two, len) == 0;
}

void JSONParser::ReportError(JSONReader::JsonParseError code,
                             int column_adjust) {
  error_code_ = code;
  error_line_ = line_number_;
  error_column_ = index_ - index_last_line_ + column_adjust;
}

// static
std::string JSONParser::FormatErrorMessage(int line, int column,
                                           const std::string& description) {
  if (line || column) {
    return StringPrintf("Line: %i, column: %i, %s",
        line, column, description.c_str());
  }
  return description;
}

}  // namespace internal
}  // namespace base
