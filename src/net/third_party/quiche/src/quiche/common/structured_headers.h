// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_STRUCTURED_HEADERS_H_
#define QUICHE_COMMON_STRUCTURED_HEADERS_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/strings/string_view.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace quiche {
namespace structured_headers {

// This file implements parsing of HTTP structured headers, as defined in
// RFC8941 (https://www.rfc-editor.org/rfc/rfc8941.html). For compatibility with
// the shipped implementation of Web Packaging, this file also supports a
// previous revision of the standard, referred to here as "Draft 9".
// (https://datatracker.ietf.org/doc/draft-ietf-httpbis-header-structure/09/)
//
// The major difference between the two revisions is in the various list
// formats: Draft 9 describes "parameterised lists" and "lists-of-lists", while
// the final RFC uses a single "list" syntax, whose members may be inner lists.
// There should be no ambiguity, however, as the code which calls this parser
// should be expecting only a single type for a given header.
//
// References within the code are tagged with either [SH09] or [RFC8941],
// depending on which revision they refer to.
//
// Currently supported data types are:
//  Item:
//   integer: 123
//   string: "abc"
//   token: abc
//   byte sequence: *YWJj*
//  Parameterised list: abc_123;a=1;b=2; cdef_456, ghi;q="9";r="w"
//  List-of-lists: "foo";"bar", "baz", "bat"; "one"
//  List: "foo", "bar", "It was the best of times."
//        ("foo" "bar"), ("baz"), ("bat" "one"), ()
//        abc;a=1;b=2; cde_456, (ghi jkl);q="9";r=w
//  Dictionary: a=(1 2), b=3, c=4;aa=bb, d=(5 6);valid=?0
//
// Functions are provided to parse each of these, which are intended to be
// called with the complete value of an HTTP header (that is, any
// sub-structure will be handled internally by the parser; the exported
// functions are not intended to be called on partial header strings.) Input
// values should be ASCII byte strings (non-ASCII characters should not be
// present in Structured Header values, and will cause the entire header to fail
// to parse.)

class QUICHE_EXPORT Item {
 public:
  enum ItemType {
    kNullType,
    kIntegerType,
    kDecimalType,
    kStringType,
    kTokenType,
    kByteSequenceType,
    kBooleanType
  };
  Item();
  explicit Item(int64_t value);
  explicit Item(double value);
  explicit Item(bool value);

  // Constructors for string-like items: Strings, Tokens and Byte Sequences.
  Item(const char* value, Item::ItemType type = kStringType);
  Item(std::string value, Item::ItemType type = kStringType);

  Item(const Item&);
  Item& operator=(const Item&);

  Item(Item&&);
  Item& operator=(Item&&);

  ~Item();

  QUICHE_EXPORT friend bool operator==(const Item&, const Item&);

  bool is_null() const { return Type() == kNullType; }
  bool is_integer() const { return Type() == kIntegerType; }
  bool is_decimal() const { return Type() == kDecimalType; }
  bool is_string() const { return Type() == kStringType; }
  bool is_token() const { return Type() == kTokenType; }
  bool is_byte_sequence() const { return Type() == kByteSequenceType; }
  bool is_boolean() const { return Type() == kBooleanType; }

  int64_t GetInteger() const {
    const auto* value = GetIfInteger();
    QUICHE_CHECK(value);
    return *value;
  }
  double GetDecimal() const {
    const auto* value = GetIfDecimal();
    QUICHE_CHECK(value);
    return *value;
  }
  bool GetBoolean() const {
    const auto* value = GetIfBoolean();
    QUICHE_CHECK(value);
    return *value;
  }
  // TODO(apaseltiner): Remove this once all callers have been migrated to
  // `GetString()`.
  // Deprecated: Use `GetString()` instead.
  const std::string& GetStringStrict() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return GetString();
  }
  const std::string& GetString() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    const auto* value = GetIfString();
    QUICHE_CHECK(value);
    return *value;
  }
  const std::string& GetToken() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    const auto* value = GetIfToken();
    QUICHE_CHECK(value);
    return *value;
  }
  const std::string& GetByteSequence() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    const auto* value = GetIfByteSequence();
    QUICHE_CHECK(value);
    return *value;
  }

  const int64_t* GetIfInteger() const ABSL_ATTRIBUTE_LIFETIME_BOUND;
  int64_t* GetIfInteger() ABSL_ATTRIBUTE_LIFETIME_BOUND;

  const double* GetIfDecimal() const ABSL_ATTRIBUTE_LIFETIME_BOUND;
  double* GetIfDecimal() ABSL_ATTRIBUTE_LIFETIME_BOUND;

  const std::string* GetIfToken() const ABSL_ATTRIBUTE_LIFETIME_BOUND;
  std::string* GetIfToken() ABSL_ATTRIBUTE_LIFETIME_BOUND;

  const std::string* GetIfString() const ABSL_ATTRIBUTE_LIFETIME_BOUND;
  std::string* GetIfString() ABSL_ATTRIBUTE_LIFETIME_BOUND;

  const std::string* GetIfByteSequence() const ABSL_ATTRIBUTE_LIFETIME_BOUND;
  std::string* GetIfByteSequence() ABSL_ATTRIBUTE_LIFETIME_BOUND;

  const bool* GetIfBoolean() const ABSL_ATTRIBUTE_LIFETIME_BOUND;
  bool* GetIfBoolean() ABSL_ATTRIBUTE_LIFETIME_BOUND;

  ItemType Type() const { return static_cast<ItemType>(value_.index()); }

 private:
  friend class StructuredHeaderSerializer;

  // Wrapper types to permit simplified use of `std::visit`.
  struct Token {
    std::string value;

    friend bool operator==(const Token&, const Token&) = default;
  };

  struct ByteSequence {
    std::string value;

    friend bool operator==(const ByteSequence&, const ByteSequence&) = default;
  };

  std::variant<std::monostate, int64_t, double, std::string, Token,
               ByteSequence, bool>
      value_;
};

// Returns a human-readable representation of an ItemType.
QUICHE_EXPORT absl::string_view ItemTypeToString(Item::ItemType type);

// Returns `true` if the string is a valid Token value.
QUICHE_EXPORT bool IsValidToken(absl::string_view str);

// Holds a ParameterizedIdentifier (draft 9 only). The contained Item must be a
// Token, and there may be any number of parameters. Parameter ordering is not
// significant.
struct QUICHE_EXPORT ParameterisedIdentifier {
  using Parameters = std::map<std::string, Item, std::less<>>;

  Item identifier;
  Parameters params;

  ParameterisedIdentifier();
  ParameterisedIdentifier(const ParameterisedIdentifier&);
  ParameterisedIdentifier& operator=(const ParameterisedIdentifier&);
  ParameterisedIdentifier(ParameterisedIdentifier&&);
  ParameterisedIdentifier& operator=(ParameterisedIdentifier&&);
  ParameterisedIdentifier(Item, Parameters);
  ~ParameterisedIdentifier();

  friend bool operator==(const ParameterisedIdentifier&,
                         const ParameterisedIdentifier&) = default;
};

using Parameters = std::vector<std::pair<std::string, Item>>;

struct QUICHE_EXPORT ParameterizedItem {
  Item item;
  Parameters params;

  ParameterizedItem();
  ParameterizedItem(const ParameterizedItem&);
  ParameterizedItem& operator=(const ParameterizedItem&);
  ParameterizedItem(ParameterizedItem&&);
  ParameterizedItem& operator=(ParameterizedItem&&);
  ParameterizedItem(Item, Parameters);
  ~ParameterizedItem();

  friend bool operator==(const ParameterizedItem&,
                         const ParameterizedItem&) = default;
};

// Holds a ParameterizedMember, which may be either a single Item, or an Inner
// List of ParameterizedItems, along with any number of parameters. Parameter
// ordering is significant.
//
// TODO(b/517204961): Replace the `member`, `member_is_inner_list`, and `params`
// fields with `std::variant<ParameterizedItem, InnerList>`.
struct QUICHE_EXPORT ParameterizedMember {
  // Constructor for a member that is an inner list.
  ParameterizedMember(std::vector<ParameterizedItem>, Parameters);

  // Constructor for a member that is a single Item.
  ParameterizedMember(Item, Parameters);

  ParameterizedMember(const ParameterizedMember&);
  ParameterizedMember& operator=(const ParameterizedMember&);

  ParameterizedMember(ParameterizedMember&&);
  ParameterizedMember& operator=(ParameterizedMember&&);

  ~ParameterizedMember();

  // Returns the item and its parameters if the member is an item,
  // `std::nullopt` otherwise.
  std::optional<std::pair<const Item&, const Parameters&>> GetWithParamsIfItem()
      const ABSL_ATTRIBUTE_LIFETIME_BOUND;

  // Returns the item and its parameters if the member is an item,
  // `std::nullopt` otherwise.
  std::optional<std::pair<Item&, Parameters&>> GetWithParamsIfItem()
      ABSL_ATTRIBUTE_LIFETIME_BOUND;

  // Returns the inner list's items and its parameters if the member is an
  // inner list, `std::nullopt` otherwise.
  std::optional<
      std::pair<const std::vector<ParameterizedItem>&, const Parameters&>>
  GetWithParamsIfInnerList() const ABSL_ATTRIBUTE_LIFETIME_BOUND;

  // Returns the inner list's items and its parameters if the member is an
  // inner list, `std::nullopt` otherwise.
  std::optional<std::pair<std::vector<ParameterizedItem>&, Parameters&>>
  GetWithParamsIfInnerList() ABSL_ATTRIBUTE_LIFETIME_BOUND;

  friend bool operator==(const ParameterizedMember&,
                         const ParameterizedMember&) = default;

  // Deprecated: Explicitly initialize the value to either an inner list or
  // an item using one of the above constructors, or wrap the value in
  // `std::optional`. This constructor shouldn't really exist, as it's not clear
  // what the default should actually be, but it is convenient for code that
  // defers assignment. As is, it produces an invalid value with
  // `member.empty() && !member_is_inner_list`.
  ParameterizedMember();

  // Deprecated: Use either of the two-argument constructors depending on
  // whether the value is an inner list or an item.
  ParameterizedMember(std::vector<ParameterizedItem>, bool member_is_inner_list,
                      Parameters);

  // Deprecated: Use `GetWithParamsIfItem()` / `GetWithParamsIfInnerList()`
  // instead.
  std::vector<ParameterizedItem> member;

  // If false, then |member| should only hold one Item.
  // Deprecated: Use `GetWithParamsIfItem()` / `GetWithParamsIfInnerList()`
  // instead.
  bool member_is_inner_list = false;

  // Deprecated: Use `GetWithParamsIfItem()` / `GetWithParamsIfInnerList()`
  // instead.
  Parameters params;
};

using DictionaryMember = std::pair<std::string, ParameterizedMember>;

// Structured Headers RFC8941 Dictionary.
class QUICHE_EXPORT Dictionary {
 public:
  using iterator = std::vector<DictionaryMember>::iterator;
  using const_iterator = std::vector<DictionaryMember>::const_iterator;
  using key_type = std::string;
  using mapped_type = ParameterizedMember;
  using value_type = std::pair<const std::string, ParameterizedMember>;

  Dictionary();
  Dictionary(const Dictionary&);
  Dictionary(Dictionary&&);
  explicit Dictionary(std::vector<DictionaryMember> members);
  ~Dictionary();
  Dictionary& operator=(const Dictionary&);
  Dictionary& operator=(Dictionary&&);

  iterator begin();
  const_iterator begin() const;
  iterator end();
  const_iterator end() const;

  // operator[](size_t) and at(size_t) will both abort the program in case of
  // out of bounds access.
  ParameterizedMember& operator[](std::size_t idx);
  const ParameterizedMember& operator[](std::size_t idx) const;
  ParameterizedMember& at(std::size_t idx);
  const ParameterizedMember& at(std::size_t idx) const;

  // Consistent with std::map, if |key| does not exist in the Dictionary, then
  // operator[](absl::string_view) will create an entry for it, but at() will
  // abort the entire program.
  ParameterizedMember& operator[](absl::string_view key);
  ParameterizedMember& at(absl::string_view key);
  const ParameterizedMember& at(absl::string_view key) const;

  const_iterator find(absl::string_view key) const;
  iterator find(absl::string_view key);

  void clear();

  bool empty() const;
  std::size_t size() const;
  bool contains(absl::string_view key) const;

  friend bool operator==(const Dictionary&, const Dictionary&) = default;

 private:
  // Uses a vector to hold pairs of key and dictionary member. This makes
  // look up by index and serialization much easier.
  std::vector<DictionaryMember> members_;
};

// Structured Headers Draft 09 Parameterised List.
using ParameterisedList = std::vector<ParameterisedIdentifier>;
// Structured Headers Draft 09 List of Lists.
using ListOfLists = std::vector<std::vector<Item>>;
// Structured Headers RFC8941 List.
using List = std::vector<ParameterizedMember>;

// Returns the result of parsing the header value as an Item, if it can be
// parsed as one, or nullopt if it cannot. Note that this uses the RFC 8941
// parsing rules, and so applies tighter range limits to integers.
//
// When `strict` is true, trailing decimal points are prohibited and byte
// sequences must strictly conform to the specification.
QUICHE_EXPORT std::optional<ParameterizedItem> ParseItem(absl::string_view str,
                                                         bool strict = false);

// Returns the result of parsing the header value as an Item with no parameters,
// or nullopt if it cannot. Note that this uses the RFC 8941 parsing rules, and
// so applies tighter range limits to integers.
//
// When `strict` is true, trailing decimal points are prohibited and byte
// sequences must strictly conform to the specification.
QUICHE_EXPORT std::optional<Item> ParseBareItem(absl::string_view str,
                                                bool strict = false);

// Returns the result of parsing the header value as a Parameterised List, if it
// can be parsed as one, or nullopt if it cannot. Note that parameter keys will
// be returned as strings, which are guaranteed to be ASCII-encoded. List items,
// as well as parameter values, will be returned as Items. This method uses the
// Draft 09 parsing rules for Items, so integers have the 64-bit int range.
// Structured-Headers Draft 09 only.
QUICHE_EXPORT std::optional<ParameterisedList> ParseParameterisedList(
    absl::string_view str);

// Returns the result of parsing the header value as a List of Lists, if it can
// be parsed as one, or nullopt if it cannot. Inner list items will be returned
// as Items. This method uses the Draft 09 parsing rules for Items, so integers
// have the 64-bit int range.
// Structured-Headers Draft 09 only.
QUICHE_EXPORT std::optional<ListOfLists> ParseListOfLists(
    absl::string_view str);

// Returns the result of parsing the header value as a general List, if it can
// be parsed as one, or nullopt if it cannot. RFC 8941 only.
//
// When `strict` is true, trailing decimal points are prohibited and byte
// sequences must strictly conform to the specification.
QUICHE_EXPORT std::optional<List> ParseList(absl::string_view str,
                                            bool strict = false);

// Returns the result of parsing the header value as a general Dictionary, if it
// can be parsed as one, or nullopt if it cannot. RFC 8941 only.
//
// When `strict` is true, trailing decimal points are prohibited and byte
// sequences must strictly conform to the specification.
QUICHE_EXPORT std::optional<Dictionary> ParseDictionary(absl::string_view str,
                                                        bool strict = false);

// Serialization is implemented for RFC 8941 only.
QUICHE_EXPORT std::optional<std::string> SerializeItem(const Item& value);
QUICHE_EXPORT std::optional<std::string> SerializeItem(
    const ParameterizedItem& value);
QUICHE_EXPORT std::optional<std::string> SerializeList(const List& value);
QUICHE_EXPORT std::optional<std::string> SerializeDictionary(
    const Dictionary& value);

}  // namespace structured_headers
}  // namespace quiche

#endif  // QUICHE_COMMON_STRUCTURED_HEADERS_H_
