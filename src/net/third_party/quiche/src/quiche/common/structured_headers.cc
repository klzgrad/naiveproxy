// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/common/structured_headers.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/attributes.h"
#include "absl/container/flat_hash_set.h"
#include "absl/functional/overload.h"
#include "absl/strings/ascii.h"
#include "absl/strings/escaping.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/common/platform/api/quiche_client_stats.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace quiche {
namespace structured_headers {

namespace {

#define DIGIT "0123456789"
#define LCALPHA "abcdefghijklmnopqrstuvwxyz"
#define UCALPHA "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
#define TCHAR DIGIT LCALPHA UCALPHA "!#$%&'*+-.^_`|~"
// https://tools.ietf.org/html/draft-ietf-httpbis-header-structure-09#section-3.9
constexpr char kTokenChars09[] = DIGIT UCALPHA LCALPHA "_-.:%*/";
// https://www.rfc-editor.org/rfc/rfc8941.html#section-3.3.4
constexpr char kTokenChars[] = TCHAR ":/";
// https://tools.ietf.org/html/draft-ietf-httpbis-header-structure-09#section-3.1
constexpr char kKeyChars09[] = DIGIT LCALPHA "_-";
// https://www.rfc-editor.org/rfc/rfc8941.html#section-3.1.2
constexpr char kKeyChars[] = DIGIT LCALPHA "_-.*";
constexpr char kSP[] = " ";
constexpr char kOWS[] = " \t";
// https://www.rfc-editor.org/rfc/rfc8941.html#section-4.2.7
// https://datatracker.ietf.org/doc/html/draft-ietf-httpbis-header-structure-09#section-4.2.11
constexpr char kBase64Chars[] = DIGIT UCALPHA LCALPHA "+/=";
#undef DIGIT
#undef LCALPHA
#undef UCALPHA

// Decodes a base64-encoded string, synthesizing padding if necessary.
// https://www.rfc-editor.org/rfc/rfc8941.html#section-4.2.7
std::optional<std::string> LenientBase64Decode(absl::string_view s) {
  std::string base64(s);
  base64.resize((base64.size() + 3) / 4 * 4, '=');
  std::string binary;
  if (!absl::Base64Unescape(base64, &binary)) {
    return std::nullopt;
  }
  return binary;
}

// Decodes a base64-encoded string, but only if it strictly follows the RFC 8941
// alphabet (no whitespace, no characters outside ALPHA, DIGIT, +, /, =).
std::optional<std::string> StrictBase64Decode(absl::string_view s) {
  if (s.find_first_not_of(kBase64Chars) != absl::string_view::npos) {
    return std::nullopt;
  }
  std::string binary;
  if (!absl::Base64Unescape(s, &binary)) {
    return std::nullopt;
  }
  return binary;
}

// https://www.rfc-editor.org/rfc/rfc8941.html#section-3.3.1
constexpr int64_t kMaxInteger = 999'999'999'999'999L;
constexpr int64_t kMinInteger = -999'999'999'999'999L;

// Smallest value which is too large for an sh-decimal. This is the smallest
// double which will round up to 1e12 when serialized, which exceeds the range
// for sh-decimal. Any float less than this should round down. This behaviour is
// verified by unit tests.
constexpr double kTooLargeDecimal = 1e12 - 0.0005;

// Removes characters in remove from the beginning of s.
void StripLeft(absl::string_view& s, absl::string_view remove) {
  size_t i = s.find_first_not_of(remove);
  if (i == absl::string_view::npos) {
    i = s.size();
  }
  s.remove_prefix(i);
}

// Parser for (a subset of) Structured Headers for HTTP defined in [SH09] and
// [RFC8941]. [SH09] compatibility is retained for use by Web Packaging, and can
// be removed once that spec is updated, and users have migrated to new headers.
// [SH09] https://tools.ietf.org/html/draft-ietf-httpbis-header-structure-09
// [RFC8941] https://www.rfc-editor.org/rfc/rfc8941.html
class StructuredHeaderParser {
 public:
  enum DraftVersion {
    kDraft09,
    kFinal,
  };
  explicit StructuredHeaderParser(absl::string_view str, DraftVersion version,
                                  bool strict)
      : input_(str), version_(version), strict_(strict) {
    // [SH09] 4.2 Step 1.
    // Discard any leading OWS from input_string.
    // [RFC8941] 4.2 Step 2.
    // Discard any leading SP characters from input_string.
    SkipWhitespaces();
  }
  StructuredHeaderParser(const StructuredHeaderParser&) = delete;
  StructuredHeaderParser& operator=(const StructuredHeaderParser&) = delete;

  // Callers should call this after ReadSomething(), to check if parser has
  // consumed all the input successfully.
  bool FinishParsing() {
    // [SH09] 4.2 Step 7.
    // Discard any leading OWS from input_string.
    // [RFC8941] 4.2 Step 6.
    // Discard any leading SP characters from input_string.
    SkipWhitespaces();
    // [SH09] 4.2 Step 8. [RFC8941] 4.2 Step 7.
    // If input_string is not empty, fail parsing.
    return input_.empty();
  }

  // Parses a List of Lists ([SH09] 4.2.4).
  std::optional<ListOfLists> ReadListOfLists() {
    QUICHE_CHECK_EQ(version_, kDraft09);
    ListOfLists result;
    while (true) {
      std::vector<Item> inner_list;
      while (true) {
        std::optional<Item> item(ReadBareItem());
        if (!item) return std::nullopt;
        inner_list.push_back(std::move(*item));
        SkipWhitespaces();
        if (!ConsumeChar(';')) break;
        SkipWhitespaces();
      }
      result.push_back(std::move(inner_list));
      SkipWhitespaces();
      if (!ConsumeChar(',')) break;
      SkipWhitespaces();
    }
    return result;
  }

  // Parses a List ([RFC8941] 4.2.1).
  std::optional<List> ReadList() {
    QUICHE_CHECK_EQ(version_, kFinal);
    List members;
    while (!input_.empty()) {
      std::optional<ParameterizedMember> member(ReadItemOrInnerList());
      if (!member) return std::nullopt;
      members.push_back(std::move(*member));
      SkipOWS();
      if (input_.empty()) break;
      if (!ConsumeChar(',')) return std::nullopt;
      SkipOWS();
      if (input_.empty()) return std::nullopt;
    }
    return members;
  }

  // Parses an Item ([RFC8941] 4.2.3).
  std::optional<ParameterizedItem> ReadItem() {
    std::optional<Item> item = ReadBareItem();
    if (!item) return std::nullopt;
    std::optional<Parameters> parameters = ReadParameters();
    if (!parameters) return std::nullopt;
    return ParameterizedItem(std::move(*item), std::move(*parameters));
  }

  // Parses a bare Item ([RFC8941] 4.2.3.1, though this is also the algorithm
  // for parsing an Item from [SH09] 4.2.7).
  std::optional<Item> ReadBareItem() {
    if (input_.empty()) {
      QUICHE_DVLOG(1) << "ReadBareItem: unexpected EOF";
      return std::nullopt;
    }
    switch (input_.front()) {
      case '"':
        return ReadString();
      case '*':
        if (version_ == kDraft09) return ReadByteSequence();
        return ReadToken();
      case ':':
        if (version_ == kFinal) return ReadByteSequence();
        return std::nullopt;
      case '?':
        return ReadBoolean();
      default:
        if (input_.front() == '-' || absl::ascii_isdigit(input_.front()))
          return ReadNumber();
        if (absl::ascii_isalpha(input_.front())) return ReadToken();
        return std::nullopt;
    }
  }

  // Parses a Dictionary ([RFC8941] 4.2.2).
  std::optional<Dictionary> ReadDictionary() {
    QUICHE_CHECK_EQ(version_, kFinal);
    Dictionary members;
    while (!input_.empty()) {
      std::optional<absl::string_view> key(ReadKey());
      if (!key) return std::nullopt;
      std::optional<ParameterizedMember> member;
      if (ConsumeChar('=')) {
        member = ReadItemOrInnerList();
        if (!member) return std::nullopt;
      } else {
        std::optional<Parameters> parameters = ReadParameters();
        if (!parameters) return std::nullopt;
        member = ParameterizedMember(Item(true), std::move(*parameters));
      }
      members[*key] = std::move(*member);
      SkipOWS();
      if (input_.empty()) break;
      if (!ConsumeChar(',')) return std::nullopt;
      SkipOWS();
      if (input_.empty()) return std::nullopt;
    }
    return members;
  }

  // Parses a Parameterised List ([SH09] 4.2.5).
  std::optional<ParameterisedList> ReadParameterisedList() {
    QUICHE_CHECK_EQ(version_, kDraft09);
    ParameterisedList items;
    while (true) {
      std::optional<ParameterisedIdentifier> item =
          ReadParameterisedIdentifier();
      if (!item) return std::nullopt;
      items.push_back(std::move(*item));
      SkipWhitespaces();
      if (!ConsumeChar(',')) return items;
      SkipWhitespaces();
    }
  }

 private:
  // Parses a Parameterised Identifier ([SH09] 4.2.6).
  std::optional<ParameterisedIdentifier> ReadParameterisedIdentifier() {
    QUICHE_CHECK_EQ(version_, kDraft09);
    std::optional<Item> primary_identifier = ReadToken();
    if (!primary_identifier) return std::nullopt;

    ParameterisedIdentifier::Parameters parameters;

    SkipWhitespaces();
    while (ConsumeChar(';')) {
      SkipWhitespaces();

      std::optional<absl::string_view> name = ReadKey();
      if (!name) return std::nullopt;

      Item value;
      if (ConsumeChar('=')) {
        auto item = ReadBareItem();
        if (!item) return std::nullopt;
        value = std::move(*item);
      }
      if (!parameters.emplace(*name, std::move(value)).second) {
        QUICHE_DVLOG(1) << "ReadParameterisedIdentifier: duplicated parameter: "
                        << *name;
        return std::nullopt;
      }
      SkipWhitespaces();
    }
    return ParameterisedIdentifier(std::move(*primary_identifier),
                                   std::move(parameters));
  }

  // Parses an Item or Inner List ([RFC8941] 4.2.1.1).
  std::optional<ParameterizedMember> ReadItemOrInnerList() {
    QUICHE_CHECK_EQ(version_, kFinal);
    bool member_is_inner_list = (!input_.empty() && input_.front() == '(');
    if (member_is_inner_list) {
      return ReadInnerList();
    } else {
      auto item = ReadItem();
      if (!item) return std::nullopt;
      return ParameterizedMember(std::move(item->item),
                                 std::move(item->params));
    }
  }

  // Parses Parameters ([RFC8941] 4.2.3.2)
  std::optional<Parameters> ReadParameters() {
    Parameters parameters;
    absl::flat_hash_set<absl::string_view> keys;

    while (ConsumeChar(';')) {
      SkipWhitespaces();

      std::optional<absl::string_view> name = ReadKey();
      if (!name) return std::nullopt;
      bool is_duplicate_key = !keys.insert(*name).second;

      Item value{true};
      if (ConsumeChar('=')) {
        auto item = ReadBareItem();
        if (!item) return std::nullopt;
        value = std::move(*item);
      }
      if (is_duplicate_key) {
        for (auto& param : parameters) {
          if (param.first == name) {
            param.second = std::move(value);
            break;
          }
        }
      } else {
        parameters.emplace_back(*name, std::move(value));
      }
    }
    return parameters;
  }

  // Parses an Inner List ([RFC8941] 4.2.1.2).
  std::optional<ParameterizedMember> ReadInnerList() {
    QUICHE_CHECK_EQ(version_, kFinal);
    if (!ConsumeChar('(')) return std::nullopt;
    std::vector<ParameterizedItem> inner_list;
    while (true) {
      SkipWhitespaces();
      if (ConsumeChar(')')) {
        std::optional<Parameters> parameters = ReadParameters();
        if (!parameters) return std::nullopt;
        return ParameterizedMember(std::move(inner_list),
                                   std::move(*parameters));
      }
      auto item = ReadItem();
      if (!item) return std::nullopt;
      inner_list.push_back(std::move(*item));
      if (input_.empty() || (input_.front() != ' ' && input_.front() != ')'))
        return std::nullopt;
    }
    QUICHE_NOTREACHED();
    return std::nullopt;
  }

  // Parses a Key ([SH09] 4.2.2, [RFC8941] 4.2.3.3).
  std::optional<absl::string_view> ReadKey() ABSL_ATTRIBUTE_LIFETIME_BOUND {
    if (version_ == kDraft09) {
      if (input_.empty() || !absl::ascii_islower(input_.front())) {
        LogParseError("ReadKey", "lcalpha");
        return std::nullopt;
      }
    } else {
      if (input_.empty() ||
          (!absl::ascii_islower(input_.front()) && input_.front() != '*')) {
        LogParseError("ReadKey", "lcalpha | *");
        return std::nullopt;
      }
    }
    const char* allowed_chars =
        (version_ == kDraft09 ? kKeyChars09 : kKeyChars);
    size_t len = input_.find_first_not_of(allowed_chars);
    if (len == absl::string_view::npos) len = input_.size();
    absl::string_view key = input_.substr(0, len);
    input_.remove_prefix(len);
    return key;
  }

  // Parses a Token ([SH09] 4.2.10, [RFC8941] 4.2.6).
  std::optional<Item> ReadToken() {
    if (input_.empty() ||
        !(absl::ascii_isalpha(input_.front()) || input_.front() == '*')) {
      LogParseError("ReadToken", "ALPHA");
      return std::nullopt;
    }
    size_t len = input_.find_first_not_of(version_ == kDraft09 ? kTokenChars09
                                                               : kTokenChars);
    if (len == absl::string_view::npos) len = input_.size();
    std::string token(input_.substr(0, len));
    input_.remove_prefix(len);
    return Item(std::move(token), Item::kTokenType);
  }

  // Parses a Number ([SH09] 4.2.8, [RFC8941] 4.2.4).
  std::optional<Item> ReadNumber() {
    bool is_negative = ConsumeChar('-');
    bool is_decimal = false;
    size_t decimal_position = 0;
    size_t i = 0;
    for (; i < input_.size(); ++i) {
      if (i > 0 && input_[i] == '.' && !is_decimal) {
        is_decimal = true;
        decimal_position = i;
        continue;
      }
      if (!absl::ascii_isdigit(input_[i])) break;
    }
    if (i == 0) {
      LogParseError("ReadNumber", "DIGIT");
      return std::nullopt;
    }
    if (!is_decimal) {
      // [RFC8941] restricts the range of integers further.
      if (version_ == kFinal && i > 15) {
        LogParseError("ReadNumber", "integer too long");
        return std::nullopt;
      }
    } else {
      if (version_ != kFinal && i > 16) {
        LogParseError("ReadNumber", "float too long");
        return std::nullopt;
      }
      if (version_ == kFinal && decimal_position > 12) {
        LogParseError("ReadNumber", "decimal too long");
        return std::nullopt;
      }
      if (i - decimal_position > (version_ == kFinal ? 4 : 7)) {
        LogParseError("ReadNumber", "too many digits after decimal");
        return std::nullopt;
      }
      const bool has_trailing_decimal = i == decimal_position + 1;
      if (!strict_) {
        // Counter to track instances of b/517189418 in which trailing decimal
        // points are erroneously accepted.
        QUICHE_CLIENT_HISTOGRAM_BOOL(
            "StructuredHeaders.DecimalWithZeroFractionalDigits",
            has_trailing_decimal,
            "Whether a decimal point is erroneously accepted without any "
            "digits following it.");
      } else if (has_trailing_decimal) {
        LogParseError("ReadNumber", "no digits after decimal");
        return std::nullopt;
      }
    }
    absl::string_view output_number_string = input_.substr(0, i);
    input_.remove_prefix(i);

    if (is_decimal) {
      // Convert to a 64-bit double, and return if the conversion is
      // successful.
      double f;
      if (!absl::SimpleAtod(output_number_string, &f)) return std::nullopt;
      return Item(is_negative ? -f : f);
    } else {
      // Convert to a 64-bit signed integer, and return if the conversion is
      // successful.
      int64_t n;
      if (!absl::SimpleAtoi(output_number_string, &n)) return std::nullopt;
      QUICHE_CHECK(version_ != kFinal ||
                   (n <= kMaxInteger && n >= kMinInteger));
      return Item(is_negative ? -n : n);
    }
  }

  // Parses a String ([SH09] 4.2.9, [RFC8941] 4.2.5).
  std::optional<Item> ReadString() {
    std::string s;
    if (!ConsumeChar('"')) {
      LogParseError("ReadString", "'\"'");
      return std::nullopt;
    }
    while (!ConsumeChar('"')) {
      size_t i = 0;
      for (; i < input_.size(); ++i) {
        if (!absl::ascii_isprint(input_[i])) {
          QUICHE_DVLOG(1) << "ReadString: non printable-ASCII character";
          return std::nullopt;
        }
        if (input_[i] == '"' || input_[i] == '\\') break;
      }
      if (i == input_.size()) {
        QUICHE_DVLOG(1) << "ReadString: missing closing '\"'";
        return std::nullopt;
      }
      s.append(input_.substr(0, i));
      input_.remove_prefix(i);
      if (ConsumeChar('\\')) {
        if (input_.empty()) {
          QUICHE_DVLOG(1) << "ReadString: backslash at string end";
          return std::nullopt;
        }
        if (input_[0] != '"' && input_[0] != '\\') {
          QUICHE_DVLOG(1) << "ReadString: invalid escape";
          return std::nullopt;
        }
        s.push_back(input_.front());
        input_.remove_prefix(1);
      }
    }
    return s;
  }

  // Parses a Byte Sequence ([SH09] 4.2.11, [RFC8941] 4.2.7).
  std::optional<Item> ReadByteSequence() {
    char delimiter = (version_ == kDraft09 ? '*' : ':');
    if (!ConsumeChar(delimiter)) {
      LogParseError("ReadByteSequence", "delimiter");
      return std::nullopt;
    }
    size_t len = input_.find(delimiter);
    if (len == absl::string_view::npos) {
      QUICHE_DVLOG(1) << "ReadByteSequence: missing closing delimiter";
      return std::nullopt;
    }

    absl::string_view encoded = input_.substr(0, len);
    std::optional<std::string> binary = StrictBase64Decode(encoded);
    const bool strict_decode_succeeded = binary.has_value();
    if (!strict_) {
      if (!strict_decode_succeeded) {
        binary = LenientBase64Decode(encoded);
      }
      if (binary.has_value()) {
        QUICHE_CLIENT_HISTOGRAM_BOOL(
            "StructuredHeaders.Base64DecodingIsStrictCompliant",
            strict_decode_succeeded,
            "Recorded true when RFC 8941 base64 decoding succeeds, false "
            "when it fails but lenient legacy decoding succeeds.");
      }
    }
    if (!binary.has_value()) {
      QUICHE_DVLOG(1) << "ReadByteSequence: failed to decode base64: "
                      << encoded;
      return std::nullopt;
    }
    input_.remove_prefix(len);
    ConsumeChar(delimiter);
    return Item(std::move(*binary), Item::kByteSequenceType);
  }

  // Parses a Boolean ([RFC8941] 4.2.8).
  // Note that this only parses ?0 and ?1 forms from SH version 10+, not the
  // previous ?F and ?T, which were not needed by any consumers of SH version 9.
  std::optional<Item> ReadBoolean() {
    if (!ConsumeChar('?')) {
      LogParseError("ReadBoolean", "'?'");
      return std::nullopt;
    }
    if (ConsumeChar('1')) {
      return Item(true);
    }
    if (ConsumeChar('0')) {
      return Item(false);
    }
    return std::nullopt;
  }

  // There are several points in the specs where the handling of whitespace
  // differs between Draft 9 and the final RFC. In those cases, Draft 9 allows
  // any OWS character, while the RFC allows only a U+0020 SPACE.
  void SkipWhitespaces() {
    if (version_ == kDraft09) {
      StripLeft(input_, kOWS);
    } else {
      StripLeft(input_, kSP);
    }
  }

  void SkipOWS() { StripLeft(input_, kOWS); }

  bool ConsumeChar(char expected) {
    if (!input_.empty() && input_.front() == expected) {
      input_.remove_prefix(1);
      return true;
    }
    return false;
  }

  void LogParseError(const char* func, const char* expected) const {
    QUICHE_DVLOG(1) << func << ": " << expected << " expected, got "
                    << (input_.empty()
                            ? "EOS"
                            : "'" + std::string(input_.substr(0, 1)) + "'");
  }

  absl::string_view input_;
  const DraftVersion version_;
  const bool strict_;
};

}  // namespace

// Serializer for (a subset of) Structured Field Values for HTTP defined in
// [RFC8941]. Note that this serializer does not attempt to support [SH09].
class StructuredHeaderSerializer {
 public:
  StructuredHeaderSerializer() = default;
  ~StructuredHeaderSerializer() = default;
  StructuredHeaderSerializer(const StructuredHeaderSerializer&) = delete;
  StructuredHeaderSerializer& operator=(const StructuredHeaderSerializer&) =
      delete;

  std::string Output() && { return std::move(output_).str(); }

  // Serializes a List ([RFC8941] 4.1.1).
  [[nodiscard]] bool WriteList(const List& value) {
    bool first = true;
    for (const auto& member : value) {
      if (!first) output_ << ", ";
      if (!WriteParameterizedMember(member)) return false;
      first = false;
    }
    return true;
  }

  // Serializes an Item ([RFC8941] 4.1.3).
  [[nodiscard]] bool WriteItem(const ParameterizedItem& value) {
    if (!WriteBareItem(value.item)) return false;
    return WriteParameters(value.params);
  }

  // Serializes an Item ([RFC8941] 4.1.3).
  [[nodiscard]] bool WriteBareItem(const Item& value) {
    return std::visit(
        absl::Overload{
            [&](const std::string& string) {
              // Serializes a String ([RFC8941] 4.1.6).
              output_ << "\"";
              for (const char c : string) {
                if (!absl::ascii_isprint(c)) return false;
                if (c == '\\' || c == '\"') output_ << "\\";
                output_ << c;
              }
              output_ << "\"";
              return true;
            },
            [&](const Item::Token& token) {
              // Serializes a Token ([RFC8941] 4.1.7).
              if (!IsValidToken(token.value)) {
                return false;
              }
              output_ << token.value;
              return true;
            },
            [&](const Item::ByteSequence& byte_sequence) {
              // Serializes a Byte Sequence ([RFC8941] 4.1.8).
              output_ << ":";
              output_ << absl::Base64Escape(byte_sequence.value);
              output_ << ":";
              return true;
            },
            [&](int64_t value) {
              // Serializes an Integer ([RFC8941] 4.1.4).
              if (value > kMaxInteger || value < kMinInteger) return false;
              output_ << value;
              return true;
            },
            [&](double decimal_value) {
              // Serializes a Decimal ([RFC8941] 4.1.5).
              if (!std::isfinite(decimal_value) ||
                  fabs(decimal_value) >= kTooLargeDecimal)
                return false;

              // Handle sign separately to simplify the rest of the formatting.
              if (decimal_value < 0) output_ << "-";
              // Unconditionally take absolute value to ensure that -0 is
              // serialized as "0.0", with no negative sign, as required by
              // spec. (4.1.5, step 2).
              decimal_value = fabs(decimal_value);
              double remainder = fmod(decimal_value, 0.002);
              if (remainder == 0.0005) {
                // Value ended in exactly 0.0005, 0.0025, 0.0045, etc. Round
                // down.
                decimal_value -= 0.0005;
              } else if (remainder == 0.0015) {
                // Value ended in exactly 0.0015, 0.0035, 0,0055, etc. Round up.
                decimal_value += 0.0005;
              } else {
                // Standard rounding will work in all other cases.
                decimal_value = round(decimal_value * 1000.0) / 1000.0;
              }

              // Use standard library functions to write the decimal, and then
              // truncate if necessary to conform to spec.

              // Maximum is 12 integer digits, one decimal point, three
              // fractional digits, and a null terminator.
              char buffer[17];
              absl::SNPrintF(buffer, std::size(buffer), "%#.3f", decimal_value);

              // Strip any trailing 0s after the decimal point, but leave at
              // least one digit after it in all cases. (So 1.230 becomes 1.23,
              // but 1.000 becomes 1.0.)
              absl::string_view formatted_number(buffer);
              auto truncate_index = formatted_number.find_last_not_of('0');
              if (formatted_number[truncate_index] == '.') truncate_index++;
              output_ << formatted_number.substr(0, truncate_index + 1);
              return true;
            },
            [&](bool value) {
              // Serializes a Boolean ([RFC8941] 4.1.9).
              output_ << (value ? "?1" : "?0");
              return true;
            },
            [](std::monostate) { return false; },
        },
        value.value_);
  }

  // Serializes a Dictionary ([RFC8941] 4.1.2).
  [[nodiscard]] bool WriteDictionary(const Dictionary& value) {
    bool first = true;
    for (const auto& [dict_key, dict_value] : value) {
      if (!first) output_ << ", ";
      if (!WriteKey(dict_key)) return false;
      first = false;
      if (!dict_value.member_is_inner_list && !dict_value.member.empty() &&
          IsBooleanTrue(dict_value.member.front().item)) {
        if (!WriteParameters(dict_value.params)) return false;
      } else {
        output_ << "=";
        if (!WriteParameterizedMember(dict_value)) return false;
      }
    }
    return true;
  }

 private:
  static bool IsBooleanTrue(const Item& item) {
    const bool* value = item.GetIfBoolean();
    return value && *value;
  }

  [[nodiscard]] bool WriteParameterizedMember(
      const ParameterizedMember& value) {
    // Serializes a parameterized member ([RFC8941] 4.1.1).
    if (value.member_is_inner_list) {
      if (!WriteInnerList(value.member)) return false;
    } else {
      QUICHE_CHECK_EQ(value.member.size(), 1UL);
      if (!WriteItem(value.member[0])) return false;
    }
    return WriteParameters(value.params);
  }

  [[nodiscard]] bool WriteInnerList(
      const std::vector<ParameterizedItem>& value) {
    // Serializes an inner list ([RFC8941] 4.1.1.1).
    output_ << "(";
    bool first = true;
    for (const ParameterizedItem& member : value) {
      if (!first) output_ << " ";
      if (!WriteItem(member)) return false;
      first = false;
    }
    output_ << ")";
    return true;
  }

  [[nodiscard]] bool WriteParameters(const Parameters& value) {
    // Serializes a parameter list ([RFC8941] 4.1.1.2).
    for (const auto& [param_name, param_value] : value) {
      output_ << ";";
      if (!WriteKey(param_name)) return false;
      if (param_value.is_null() || IsBooleanTrue(param_value)) continue;
      output_ << "=";
      if (!WriteBareItem(param_value)) return false;
    }
    return true;
  }

  [[nodiscard]] bool WriteKey(const std::string& value) {
    // Serializes a Key ([RFC8941] 4.1.1.3).
    if (value.empty()) return false;
    if (value.find_first_not_of(kKeyChars) != std::string::npos) return false;
    if (!absl::ascii_islower(value[0]) && value[0] != '*') return false;
    output_ << value;
    return true;
  }

  std::ostringstream output_;
};

absl::string_view ItemTypeToString(Item::ItemType type) {
  switch (type) {
    case Item::kNullType:
      return "null";
    case Item::kIntegerType:
      return "integer";
    case Item::kDecimalType:
      return "decimal";
    case Item::kStringType:
      return "string";
    case Item::kTokenType:
      return "token";
    case Item::kByteSequenceType:
      return "byte sequence";
    case Item::kBooleanType:
      return "boolean";
  }
  return "[invalid type]";
}

bool IsValidToken(absl::string_view str) {
  // Validate Token value per [RFC8941] 4.1.7.
  if (str.empty() ||
      !(absl::ascii_isalpha(str.front()) || str.front() == '*')) {
    return false;
  }
  if (str.find_first_not_of(kTokenChars) != std::string::npos) {
    return false;
  }
  return true;
}

Item::Item() = default;
Item::Item(std::string value, Item::ItemType type) {
  switch (type) {
    case kStringType:
      value_.emplace<kStringType>(std::move(value));
      break;
    case kTokenType:
      value_.emplace<kTokenType>(std::move(value));
      break;
    case kByteSequenceType:
      value_.emplace<kByteSequenceType>(std::move(value));
      break;
    default:
      QUICHE_CHECK(false);
      break;
  }
}
Item::Item(const char* value, Item::ItemType type)
    : Item(std::string(value), type) {}
Item::Item(int64_t value) : value_(value) {}
Item::Item(double value) : value_(value) {}
Item::Item(bool value) : value_(value) {}

Item::Item(const Item&) = default;
Item& Item::operator=(const Item&) = default;

Item::Item(Item&&) = default;
Item& Item::operator=(Item&&) = default;

Item::~Item() = default;

const int64_t* Item::GetIfInteger() const {
  return std::get_if<int64_t>(&value_);
}

int64_t* Item::GetIfInteger() { return std::get_if<int64_t>(&value_); }

const double* Item::GetIfDecimal() const {
  return std::get_if<double>(&value_);
}

double* Item::GetIfDecimal() { return std::get_if<double>(&value_); }

const std::string* Item::GetIfString() const {
  return std::get_if<std::string>(&value_);
}

std::string* Item::GetIfString() { return std::get_if<std::string>(&value_); }

const std::string* Item::GetIfToken() const {
  const auto* token = std::get_if<Token>(&value_);
  return token ? &token->value : nullptr;
}

std::string* Item::GetIfToken() {
  auto* token = std::get_if<Token>(&value_);
  return token ? &token->value : nullptr;
}

const std::string* Item::GetIfByteSequence() const {
  const auto* byte_sequence = std::get_if<ByteSequence>(&value_);
  return byte_sequence ? &byte_sequence->value : nullptr;
}

std::string* Item::GetIfByteSequence() {
  auto* byte_sequence = std::get_if<ByteSequence>(&value_);
  return byte_sequence ? &byte_sequence->value : nullptr;
}

const bool* Item::GetIfBoolean() const { return std::get_if<bool>(&value_); }

bool* Item::GetIfBoolean() { return std::get_if<bool>(&value_); }

// Not defaulted to work around
// https://github.com/llvm/llvm-project/issues/132249 in older Clang versions.
bool operator==(const Item& lhs, const Item& rhs) {
  return lhs.value_ == rhs.value_;
}

ParameterizedItem::ParameterizedItem() = default;
ParameterizedItem::ParameterizedItem(const ParameterizedItem&) = default;
ParameterizedItem& ParameterizedItem::operator=(const ParameterizedItem&) =
    default;
ParameterizedItem::ParameterizedItem(ParameterizedItem&&) = default;
ParameterizedItem& ParameterizedItem::operator=(ParameterizedItem&&) = default;
ParameterizedItem::ParameterizedItem(Item id, Parameters ps)
    : item(std::move(id)), params(std::move(ps)) {}
ParameterizedItem::~ParameterizedItem() = default;

ParameterizedMember::ParameterizedMember() = default;
ParameterizedMember::ParameterizedMember(const ParameterizedMember&) = default;
ParameterizedMember& ParameterizedMember::operator=(
    const ParameterizedMember&) = default;
ParameterizedMember::ParameterizedMember(ParameterizedMember&&) = default;
ParameterizedMember& ParameterizedMember::operator=(ParameterizedMember&&) =
    default;
ParameterizedMember::ParameterizedMember(std::vector<ParameterizedItem> id,
                                         bool member_is_inner_list,
                                         Parameters ps)
    : member(std::move(id)),
      member_is_inner_list(member_is_inner_list),
      params(std::move(ps)) {}
ParameterizedMember::ParameterizedMember(std::vector<ParameterizedItem> id,
                                         Parameters ps)
    : member(std::move(id)),
      member_is_inner_list(true),
      params(std::move(ps)) {}
ParameterizedMember::ParameterizedMember(Item id, Parameters ps)
    : member({{std::move(id), {}}}),
      member_is_inner_list(false),
      params(std::move(ps)) {}
ParameterizedMember::~ParameterizedMember() = default;

std::optional<std::pair<const Item&, const Parameters&>>
ParameterizedMember::GetWithParamsIfItem() const {
  // Strictly, `member.size()` should be exactly 1 when `!member_is_inner_list`,
  // but this isn't guaranteed due to to the public nature of the fields. Handle
  // the empty case here to avoid crashing or UB.
  if (member_is_inner_list || member.empty()) {
    return std::nullopt;
  }

  return std::pair<const Item&, const Parameters&>(member.front().item, params);
}

std::optional<std::pair<Item&, Parameters&>>
ParameterizedMember::GetWithParamsIfItem() {
  // Strictly, `member.size()` should be exactly 1 when `!member_is_inner_list`,
  // but this isn't guaranteed due to to the public nature of the fields. Handle
  // the empty case here to avoid crashing or UB.
  if (member_is_inner_list || member.empty()) {
    return std::nullopt;
  }

  return std::pair<Item&, Parameters&>(member.front().item, params);
}

std::optional<
    std::pair<const std::vector<ParameterizedItem>&, const Parameters&>>
ParameterizedMember::GetWithParamsIfInnerList() const {
  if (!member_is_inner_list) {
    return std::nullopt;
  }

  return std::pair<const std::vector<ParameterizedItem>&, const Parameters&>(
      member, params);
}

std::optional<std::pair<std::vector<ParameterizedItem>&, Parameters&>>
ParameterizedMember::GetWithParamsIfInnerList() {
  if (!member_is_inner_list) {
    return std::nullopt;
  }

  return std::pair<std::vector<ParameterizedItem>&, Parameters&>(member,
                                                                 params);
}

ParameterisedIdentifier::ParameterisedIdentifier() = default;
ParameterisedIdentifier::ParameterisedIdentifier(
    const ParameterisedIdentifier&) = default;
ParameterisedIdentifier& ParameterisedIdentifier::operator=(
    const ParameterisedIdentifier&) = default;
ParameterisedIdentifier::ParameterisedIdentifier(ParameterisedIdentifier&&) =
    default;
ParameterisedIdentifier& ParameterisedIdentifier::operator=(
    ParameterisedIdentifier&&) = default;
ParameterisedIdentifier::ParameterisedIdentifier(Item id, Parameters ps)
    : identifier(std::move(id)), params(std::move(ps)) {}
ParameterisedIdentifier::~ParameterisedIdentifier() = default;

Dictionary::Dictionary() = default;
Dictionary::Dictionary(const Dictionary&) = default;
Dictionary& Dictionary::operator=(const Dictionary&) = default;
Dictionary::Dictionary(Dictionary&&) = default;
Dictionary& Dictionary::operator=(Dictionary&&) = default;
Dictionary::Dictionary(std::vector<DictionaryMember> members)
    : members_(std::move(members)) {}
Dictionary::~Dictionary() = default;
Dictionary::iterator Dictionary::begin() { return members_.begin(); }
Dictionary::const_iterator Dictionary::begin() const {
  return members_.begin();
}
Dictionary::iterator Dictionary::end() { return members_.end(); }
Dictionary::const_iterator Dictionary::end() const { return members_.end(); }
ParameterizedMember& Dictionary::operator[](std::size_t idx) {
  return members_[idx].second;
}
const ParameterizedMember& Dictionary::operator[](std::size_t idx) const {
  return members_[idx].second;
}
ParameterizedMember& Dictionary::at(std::size_t idx) { return (*this)[idx]; }
const ParameterizedMember& Dictionary::at(std::size_t idx) const {
  return (*this)[idx];
}
ParameterizedMember& Dictionary::operator[](absl::string_view key) {
  auto it = find(key);
  if (it != end()) return it->second;
  return members_.emplace_back(key, ParameterizedMember()).second;
}
ParameterizedMember& Dictionary::at(absl::string_view key) {
  auto it = find(key);
  QUICHE_CHECK(it != end()) << "Provided key not found in dictionary";
  return it->second;
}
const ParameterizedMember& Dictionary::at(absl::string_view key) const {
  auto it = find(key);
  QUICHE_CHECK(it != end()) << "Provided key not found in dictionary";
  return it->second;
}
Dictionary::const_iterator Dictionary::find(absl::string_view key) const {
  return absl::c_find_if(
      members_, [key](const auto& member) { return member.first == key; });
}
Dictionary::iterator Dictionary::find(absl::string_view key) {
  return absl::c_find_if(
      members_, [key](const auto& member) { return member.first == key; });
}
bool Dictionary::empty() const { return members_.empty(); }
std::size_t Dictionary::size() const { return members_.size(); }
bool Dictionary::contains(absl::string_view key) const {
  return find(key) != end();
}
void Dictionary::clear() { members_.clear(); }

std::optional<ParameterizedItem> ParseItem(absl::string_view str, bool strict) {
  StructuredHeaderParser parser(str, StructuredHeaderParser::kFinal, strict);
  std::optional<ParameterizedItem> item = parser.ReadItem();
  if (item && parser.FinishParsing()) return item;
  return std::nullopt;
}

std::optional<Item> ParseBareItem(absl::string_view str, bool strict) {
  StructuredHeaderParser parser(str, StructuredHeaderParser::kFinal, strict);
  std::optional<Item> item = parser.ReadBareItem();
  if (item && parser.FinishParsing()) return item;
  return std::nullopt;
}

std::optional<ParameterisedList> ParseParameterisedList(absl::string_view str) {
  StructuredHeaderParser parser(str, StructuredHeaderParser::kDraft09,
                                /*strict=*/false);
  std::optional<ParameterisedList> param_list = parser.ReadParameterisedList();
  if (param_list && parser.FinishParsing()) return param_list;
  return std::nullopt;
}

std::optional<ListOfLists> ParseListOfLists(absl::string_view str) {
  StructuredHeaderParser parser(str, StructuredHeaderParser::kDraft09,
                                /*strict=*/false);
  std::optional<ListOfLists> list_of_lists = parser.ReadListOfLists();
  if (list_of_lists && parser.FinishParsing()) return list_of_lists;
  return std::nullopt;
}

std::optional<List> ParseList(absl::string_view str, bool strict) {
  StructuredHeaderParser parser(str, StructuredHeaderParser::kFinal, strict);
  std::optional<List> list = parser.ReadList();
  if (list && parser.FinishParsing()) return list;
  return std::nullopt;
}

std::optional<Dictionary> ParseDictionary(absl::string_view str, bool strict) {
  StructuredHeaderParser parser(str, StructuredHeaderParser::kFinal, strict);
  std::optional<Dictionary> dictionary = parser.ReadDictionary();
  if (dictionary && parser.FinishParsing()) return dictionary;
  return std::nullopt;
}

std::optional<std::string> SerializeItem(const Item& value) {
  StructuredHeaderSerializer s;
  if (s.WriteBareItem(value)) return std::move(s).Output();
  return std::nullopt;
}

std::optional<std::string> SerializeItem(const ParameterizedItem& value) {
  StructuredHeaderSerializer s;
  if (s.WriteItem(value)) return std::move(s).Output();
  return std::nullopt;
}

std::optional<std::string> SerializeList(const List& value) {
  StructuredHeaderSerializer s;
  if (s.WriteList(value)) return std::move(s).Output();
  return std::nullopt;
}

std::optional<std::string> SerializeDictionary(const Dictionary& value) {
  StructuredHeaderSerializer s;
  if (s.WriteDictionary(value)) return std::move(s).Output();
  return std::nullopt;
}

}  // namespace structured_headers
}  // namespace quiche
