// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/json/json_parser.h"

#include <stddef.h>

#include <memory>

#include "base/json/json_reader.h"
#include "base/memory/ptr_util.h"
#include "base/optional.h"
#include "base/stl_util.h"
#include "base/strings/stringprintf.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {
namespace internal {

class JSONParserTest : public testing::Test {
 public:
  JSONParser* NewTestParser(const std::string& input,
                            int options = JSON_PARSE_RFC) {
    JSONParser* parser = new JSONParser(options);
    parser->input_ = input;
    parser->index_ = 0;
    return parser;
  }

  void TestLastThree(JSONParser* parser) {
    EXPECT_EQ(',', *parser->PeekChar());
    parser->ConsumeChar();
    EXPECT_EQ('|', *parser->PeekChar());
    parser->ConsumeChar();
    EXPECT_EQ('\0', *parser->pos());
    EXPECT_EQ(static_cast<size_t>(parser->index_), parser->input_.length());
  }
};

TEST_F(JSONParserTest, NextChar) {
  std::string input("Hello world");
  std::unique_ptr<JSONParser> parser(NewTestParser(input));

  EXPECT_EQ('H', *parser->pos());
  for (size_t i = 1; i < input.length(); ++i) {
    parser->ConsumeChar();
    EXPECT_EQ(input[i], *parser->PeekChar());
  }
  parser->ConsumeChar();
  EXPECT_EQ('\0', *parser->pos());
  EXPECT_EQ(static_cast<size_t>(parser->index_), parser->input_.length());
}

TEST_F(JSONParserTest, ConsumeString) {
  std::string input("\"test\",|");
  std::unique_ptr<JSONParser> parser(NewTestParser(input));
  Optional<Value> value(parser->ConsumeString());
  EXPECT_EQ(',', *parser->pos());

  TestLastThree(parser.get());

  ASSERT_TRUE(value);
  std::string str;
  EXPECT_TRUE(value->GetAsString(&str));
  EXPECT_EQ("test", str);
}

TEST_F(JSONParserTest, ConsumeList) {
  std::string input("[true, false],|");
  std::unique_ptr<JSONParser> parser(NewTestParser(input));
  Optional<Value> value(parser->ConsumeList());
  EXPECT_EQ(',', *parser->pos());

  TestLastThree(parser.get());

  ASSERT_TRUE(value);
  ASSERT_TRUE(value->is_list());
  EXPECT_EQ(2u, value->GetList().size());
}

TEST_F(JSONParserTest, ConsumeDictionary) {
  std::string input("{\"abc\":\"def\"},|");
  std::unique_ptr<JSONParser> parser(NewTestParser(input));
  Optional<Value> value(parser->ConsumeDictionary());
  EXPECT_EQ(',', *parser->pos());

  TestLastThree(parser.get());

  ASSERT_TRUE(value);
  ASSERT_TRUE(value->is_dict());
  const std::string* str = value->FindStringKey("abc");
  ASSERT_TRUE(str);
  EXPECT_EQ("def", *str);
}

TEST_F(JSONParserTest, ConsumeLiterals) {
  // Literal |true|.
  std::string input("true,|");
  std::unique_ptr<JSONParser> parser(NewTestParser(input));
  Optional<Value> value(parser->ConsumeLiteral());
  EXPECT_EQ(',', *parser->pos());

  TestLastThree(parser.get());

  ASSERT_TRUE(value);
  bool bool_value = false;
  EXPECT_TRUE(value->GetAsBoolean(&bool_value));
  EXPECT_TRUE(bool_value);

  // Literal |false|.
  input = "false,|";
  parser.reset(NewTestParser(input));
  value = parser->ConsumeLiteral();
  EXPECT_EQ(',', *parser->pos());

  TestLastThree(parser.get());

  ASSERT_TRUE(value);
  EXPECT_TRUE(value->GetAsBoolean(&bool_value));
  EXPECT_FALSE(bool_value);

  // Literal |null|.
  input = "null,|";
  parser.reset(NewTestParser(input));
  value = parser->ConsumeLiteral();
  EXPECT_EQ(',', *parser->pos());

  TestLastThree(parser.get());

  ASSERT_TRUE(value);
  EXPECT_TRUE(value->is_none());
}

TEST_F(JSONParserTest, ConsumeNumbers) {
  // Integer.
  std::string input("1234,|");
  std::unique_ptr<JSONParser> parser(NewTestParser(input));
  Optional<Value> value(parser->ConsumeNumber());
  EXPECT_EQ(',', *parser->pos());

  TestLastThree(parser.get());

  ASSERT_TRUE(value);
  int number_i;
  EXPECT_TRUE(value->GetAsInteger(&number_i));
  EXPECT_EQ(1234, number_i);

  // Negative integer.
  input = "-1234,|";
  parser.reset(NewTestParser(input));
  value = parser->ConsumeNumber();
  EXPECT_EQ(',', *parser->pos());

  TestLastThree(parser.get());

  ASSERT_TRUE(value);
  EXPECT_TRUE(value->GetAsInteger(&number_i));
  EXPECT_EQ(-1234, number_i);

  // Double.
  input = "12.34,|";
  parser.reset(NewTestParser(input));
  value = parser->ConsumeNumber();
  EXPECT_EQ(',', *parser->pos());

  TestLastThree(parser.get());

  ASSERT_TRUE(value);
  double number_d;
  EXPECT_TRUE(value->GetAsDouble(&number_d));
  EXPECT_EQ(12.34, number_d);

  // Scientific.
  input = "42e3,|";
  parser.reset(NewTestParser(input));
  value = parser->ConsumeNumber();
  EXPECT_EQ(',', *parser->pos());

  TestLastThree(parser.get());

  ASSERT_TRUE(value);
  EXPECT_TRUE(value->GetAsDouble(&number_d));
  EXPECT_EQ(42000, number_d);

  // Negative scientific.
  input = "314159e-5,|";
  parser.reset(NewTestParser(input));
  value = parser->ConsumeNumber();
  EXPECT_EQ(',', *parser->pos());

  TestLastThree(parser.get());

  ASSERT_TRUE(value);
  EXPECT_TRUE(value->GetAsDouble(&number_d));
  EXPECT_EQ(3.14159, number_d);

  // Positive scientific.
  input = "0.42e+3,|";
  parser.reset(NewTestParser(input));
  value = parser->ConsumeNumber();
  EXPECT_EQ(',', *parser->pos());

  TestLastThree(parser.get());

  ASSERT_TRUE(value);
  EXPECT_TRUE(value->GetAsDouble(&number_d));
  EXPECT_EQ(420, number_d);
}

TEST_F(JSONParserTest, ErrorMessages) {
  JSONReader::ValueWithError root =
      JSONReader::ReadAndReturnValueWithError("[42]", JSON_PARSE_RFC);
  EXPECT_TRUE(root.error_message.empty());
  EXPECT_EQ(0, root.error_code);

  // Test each of the error conditions
  root = JSONReader::ReadAndReturnValueWithError("{},{}", JSON_PARSE_RFC);
  EXPECT_FALSE(root.value);
  EXPECT_EQ(JSONParser::FormatErrorMessage(
                1, 3, JSONReader::kUnexpectedDataAfterRoot),
            root.error_message);
  EXPECT_EQ(JSONReader::JSON_UNEXPECTED_DATA_AFTER_ROOT, root.error_code);

  std::string nested_json;
  for (int i = 0; i < 201; ++i) {
    nested_json.insert(nested_json.begin(), '[');
    nested_json.append(1, ']');
  }
  root = JSONReader::ReadAndReturnValueWithError(nested_json, JSON_PARSE_RFC);
  EXPECT_FALSE(root.value);
  EXPECT_EQ(JSONParser::FormatErrorMessage(1, 200, JSONReader::kTooMuchNesting),
            root.error_message);
  EXPECT_EQ(JSONReader::JSON_TOO_MUCH_NESTING, root.error_code);

  root = JSONReader::ReadAndReturnValueWithError("[1,]", JSON_PARSE_RFC);
  EXPECT_FALSE(root.value);
  EXPECT_EQ(JSONParser::FormatErrorMessage(1, 4, JSONReader::kTrailingComma),
            root.error_message);
  EXPECT_EQ(JSONReader::JSON_TRAILING_COMMA, root.error_code);

  root =
      JSONReader::ReadAndReturnValueWithError("{foo:\"bar\"}", JSON_PARSE_RFC);
  EXPECT_FALSE(root.value);
  EXPECT_EQ(
      JSONParser::FormatErrorMessage(1, 2, JSONReader::kUnquotedDictionaryKey),
      root.error_message);
  EXPECT_EQ(JSONReader::JSON_UNQUOTED_DICTIONARY_KEY, root.error_code);

  root = JSONReader::ReadAndReturnValueWithError("{\"foo\":\"bar\",}",
                                                 JSON_PARSE_RFC);
  EXPECT_FALSE(root.value);
  EXPECT_EQ(JSONParser::FormatErrorMessage(1, 14, JSONReader::kTrailingComma),
            root.error_message);

  root = JSONReader::ReadAndReturnValueWithError("[nu]", JSON_PARSE_RFC);
  EXPECT_FALSE(root.value);
  EXPECT_EQ(JSONParser::FormatErrorMessage(1, 2, JSONReader::kSyntaxError),
            root.error_message);
  EXPECT_EQ(JSONReader::JSON_SYNTAX_ERROR, root.error_code);

  root =
      JSONReader::ReadAndReturnValueWithError("[\"xxx\\xq\"]", JSON_PARSE_RFC);
  EXPECT_FALSE(root.value);
  EXPECT_EQ(JSONParser::FormatErrorMessage(1, 7, JSONReader::kInvalidEscape),
            root.error_message);
  EXPECT_EQ(JSONReader::JSON_INVALID_ESCAPE, root.error_code);

  root =
      JSONReader::ReadAndReturnValueWithError("[\"xxx\\uq\"]", JSON_PARSE_RFC);
  EXPECT_FALSE(root.value);
  EXPECT_EQ(JSONParser::FormatErrorMessage(1, 7, JSONReader::kInvalidEscape),
            root.error_message);
  EXPECT_EQ(JSONReader::JSON_INVALID_ESCAPE, root.error_code);

  root =
      JSONReader::ReadAndReturnValueWithError("[\"xxx\\q\"]", JSON_PARSE_RFC);
  EXPECT_FALSE(root.value);
  EXPECT_EQ(JSONParser::FormatErrorMessage(1, 7, JSONReader::kInvalidEscape),
            root.error_message);
  EXPECT_EQ(JSONReader::JSON_INVALID_ESCAPE, root.error_code);
}

}  // namespace internal
}  // namespace base
