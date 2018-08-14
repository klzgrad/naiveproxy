// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/input_conversion.h"

#include <memory>
#include <utility>

#include "base/json/json_reader.h"
#include "base/macros.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "tools/gn/build_settings.h"
#include "tools/gn/err.h"
#include "tools/gn/input_file.h"
#include "tools/gn/label.h"
#include "tools/gn/parse_tree.h"
#include "tools/gn/parser.h"
#include "tools/gn/scheduler.h"
#include "tools/gn/scope.h"
#include "tools/gn/settings.h"
#include "tools/gn/tokenizer.h"
#include "tools/gn/value.h"

namespace {

enum ValueOrScope {
  PARSE_VALUE,  // Treat the input as an expression.
  PARSE_SCOPE,  // Treat the input as code and return the resulting scope.
};

// Sets the origin of the value and any nested values with the given node.
Value ParseValueOrScope(const Settings* settings,
                        const std::string& input,
                        ValueOrScope what,
                        const ParseNode* origin,
                        Err* err) {
  // The memory for these will be kept around by the input file manager
  // so the origin parse nodes for the values will be preserved.
  InputFile* input_file;
  std::vector<Token>* tokens;
  std::unique_ptr<ParseNode>* parse_root_ptr;
  g_scheduler->input_file_manager()->AddDynamicInput(SourceFile(), &input_file,
                                                     &tokens, &parse_root_ptr);

  input_file->SetContents(input);
  if (origin) {
    // This description will be the blame for any error messages caused by
    // script parsing or if a value is blamed. It will say
    // "Error at <...>:line:char" so here we try to make a string for <...>
    // that reads well in this context.
    input_file->set_friendly_name("dynamically parsed input that " +
                                  origin->GetRange().begin().Describe(true) +
                                  " loaded ");
  } else {
    input_file->set_friendly_name("dynamic input");
  }

  *tokens = Tokenizer::Tokenize(input_file, err);
  if (err->has_error())
    return Value();

  // Parse the file according to what we're looking for.
  if (what == PARSE_VALUE)
    *parse_root_ptr = Parser::ParseValue(*tokens, err);
  else
    *parse_root_ptr = Parser::Parse(*tokens, err);  // Will return a Block.
  if (err->has_error())
    return Value();
  ParseNode* parse_root = parse_root_ptr->get();  // For nicer syntax below.

  // It's valid for the result to be a null pointer, this just means that the
  // script returned nothing.
  if (!parse_root)
    return Value();

  std::unique_ptr<Scope> scope = std::make_unique<Scope>(settings);
  Value result = parse_root->Execute(scope.get(), err);
  if (err->has_error())
    return Value();

  // When we want the result as a scope, the result is actually the scope
  // we made, rather than the result of running the block (which will be empty).
  if (what == PARSE_SCOPE) {
    DCHECK(result.type() == Value::NONE);
    result = Value(origin, std::move(scope));
  }
  return result;
}

Value ParseList(const std::string& input, const ParseNode* origin, Err* err) {
  Value ret(origin, Value::LIST);
  std::vector<std::string> as_lines = base::SplitString(
      input, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);

  // Trim one empty line from the end since the last line might end in a
  // newline. If the user wants more trimming, they'll specify "trim" in the
  // input conversion options.
  if (!as_lines.empty() && as_lines[as_lines.size() - 1].empty())
    as_lines.resize(as_lines.size() - 1);

  ret.list_value().reserve(as_lines.size());
  for (const auto& line : as_lines)
    ret.list_value().push_back(Value(origin, line));
  return ret;
}

bool IsIdentifier(const base::StringPiece& buffer) {
  DCHECK(buffer.size() > 0);
  if (!Tokenizer::IsIdentifierFirstChar(buffer[0]))
    return false;
  for (size_t i = 1; i < buffer.size(); i++)
    if (!Tokenizer::IsIdentifierContinuingChar(buffer[i]))
      return false;
  return true;
}

Value ParseJSONValue(const Settings* settings,
                     const base::Value& value,
                     const ParseNode* origin,
                     InputFile* input_file,
                     Err* err) {
  switch (value.type()) {
    case base::Value::Type::NONE:
      *err = Err(origin, "Null values are not supported.");
      return Value();
    case base::Value::Type::BOOLEAN:
      return Value(origin, value.GetBool());
    case base::Value::Type::INTEGER:
      return Value(origin, static_cast<int64_t>(value.GetInt()));
    case base::Value::Type::STRING:
      return Value(origin, value.GetString());
    case base::Value::Type::BINARY:
      *err = Err(origin, "Binary values are not supported.");
      return Value();
    case base::Value::Type::DICTIONARY: {
      std::unique_ptr<Scope> scope = std::make_unique<Scope>(settings);
      for (const auto& it : value.DictItems()) {
        Value parsed_value =
            ParseJSONValue(settings, it.second, origin, input_file, err);
        if (!IsIdentifier(it.first)) {
          *err = Err(origin, "Invalid identifier \"" + it.first + "\".");
          return Value();
        }
        // Search for the key in the input file. We know it's present because
        // it was parsed by the JSON reader, but we need its location to
        // construct a StringPiece that can be used as key in the Scope.
        size_t off = input_file->contents().find("\"" + it.first + "\"");
        if (off == std::string::npos) {
          *err = Err(origin, "Invalid encoding \"" + it.first + "\".");
          return Value();
        }
        base::StringPiece key(&input_file->contents()[off + 1],
                              it.first.size());
        scope->SetValue(key, std::move(parsed_value), origin);
      }
      return Value(origin, std::move(scope));
    }
    case base::Value::Type::LIST: {
      Value result(origin, Value::LIST);
      result.list_value().reserve(value.GetList().size());
      for (const auto& val : value.GetList()) {
        Value parsed_value =
            ParseJSONValue(settings, val, origin, input_file, err);
        result.list_value().push_back(parsed_value);
      }
      return result;
    }
  }
  return Value();
}

// Parses the JSON string and converts it to GN value.
Value ParseJSON(const Settings* settings,
                const std::string& input,
                const ParseNode* origin,
                Err* err) {
  InputFile* input_file;
  std::vector<Token>* tokens;
  std::unique_ptr<ParseNode>* parse_root_ptr;
  g_scheduler->input_file_manager()->AddDynamicInput(SourceFile(), &input_file,
                                                     &tokens, &parse_root_ptr);
  input_file->SetContents(input);

  int error_code_out;
  std::string error_msg_out;
  std::unique_ptr<base::Value> value = base::JSONReader::ReadAndReturnError(
      input, base::JSONParserOptions::JSON_PARSE_RFC, &error_code_out,
      &error_msg_out);
  if (!value) {
    *err = Err(origin, "Input is not a valid JSON: " + error_msg_out);
    return Value();
  }

  return ParseJSONValue(settings, *value, origin, input_file, err);
}

// Backend for ConvertInputToValue, this takes the extracted string for the
// input conversion so we can recursively call ourselves to handle the optional
// "trim" prefix. This original value is also kept for the purposes of throwing
// errors.
Value DoConvertInputToValue(const Settings* settings,
                            const std::string& input,
                            const ParseNode* origin,
                            const Value& original_input_conversion,
                            const std::string& input_conversion,
                            Err* err) {
  if (input_conversion.empty())
    return Value();  // Empty string means discard the result.

  const char kTrimPrefix[] = "trim ";
  if (base::StartsWith(input_conversion, kTrimPrefix,
                       base::CompareCase::SENSITIVE)) {
    std::string trimmed;
    base::TrimWhitespaceASCII(input, base::TRIM_ALL, &trimmed);

    // Remove "trim" prefix from the input conversion and re-run.
    return DoConvertInputToValue(
        settings, trimmed, origin, original_input_conversion,
        input_conversion.substr(arraysize(kTrimPrefix) - 1), err);
  }

  if (input_conversion == "value")
    return ParseValueOrScope(settings, input, PARSE_VALUE, origin, err);
  if (input_conversion == "string")
    return Value(origin, input);
  if (input_conversion == "list lines")
    return ParseList(input, origin, err);
  if (input_conversion == "scope")
    return ParseValueOrScope(settings, input, PARSE_SCOPE, origin, err);
  if (input_conversion == "json")
    return ParseJSON(settings, input, origin, err);

  *err = Err(original_input_conversion, "Not a valid input_conversion.",
             "Run gn help input_conversion to see your options.");
  return Value();
}

}  // namespace

const char kInputOutputConversion_Help[] =
    R"(Input and output conversions are arguments to file and process functions
that specify how to convert data to or from external formats. The possible
values for parameters specifying conversions are:

  "" (the default)
      input: Discard the result and return None.

      output: If value is a list, then "list lines"; otherwise "value".

  "list lines"
      input:
        Return the file contents as a list, with a string for each line. The
        newlines will not be present in the result. The last line may or may not
        end in a newline.

        After splitting, each individual line will be trimmed of whitespace on
        both ends.

      output:
        Renders the value contents as a list, with a string for each line. The
        newlines will not be present in the result. The last line will end in with
        a newline.

  "scope"
      input:
        Execute the block as GN code and return a scope with the resulting values
        in it. If the input was:
          a = [ "hello.cc", "world.cc" ]
          b = 26
        and you read the result into a variable named "val", then you could
        access contents the "." operator on "val":
          sources = val.a
          some_count = val.b

      output:
        Renders the value contents as a GN code block, reversing the input
        result above.

  "string"
      input: Return the file contents into a single string.

      output:
        Render the value contents into a single string. The output is:
        a string renders with quotes, e.g. "str"
        an integer renders as a stringified integer, e.g. "6"
        a boolean renders as the associated string, e.g. "true"
        a list renders as a representation of its contents, e.g. "[\"str\", 6]"
        a scope renders as a GN code block of its values. If the Value was:
            Value val;
            val.a = [ "hello.cc", "world.cc" ];
            val.b = 26
          the resulting output would be:
            "{
                a = [ \"hello.cc\", \"world.cc\" ]
                b = 26
            }"

  "value"
      input:
        Parse the input as if it was a literal rvalue in a buildfile. Examples of
        typical program output using this mode:
          [ "foo", "bar" ]     (result will be a list)
        or
          "foo bar"            (result will be a string)
        or
          5                    (result will be an integer)

        Note that if the input is empty, the result will be a null value which
        will produce an error if assigned to a variable.

      output:
        Render the value contents as a literal rvalue. Strings render with escaped
        quotes.

  "json"
      input: Parse the input as a JSON and convert it to equivalent GN rvalue.

      output: Convert the Value to equivalent JSON value.

      The data type mapping is:
        a string in JSON maps to string in GN
        an integer in JSON maps to integer in GN
        a float in JSON is unsupported and will result in an error
        an object in JSON maps to scope in GN
        an array in JSON maps to list in GN
        a boolean in JSON maps to boolean in GN
        a null in JSON is unsupported and will result in an error

      Nota that the input dictionary keys have to be valid GN identifiers
      otherwise they will produce an error.

  "trim ..." (input only)
      Prefixing any of the other transformations with the word "trim" will
      result in whitespace being trimmed from the beginning and end of the
      result before processing.

      Examples: "trim string" or "trim list lines"

      Note that "trim value" is useless because the value parser skips
      whitespace anyway.
)";

Value ConvertInputToValue(const Settings* settings,
                          const std::string& input,
                          const ParseNode* origin,
                          const Value& input_conversion_value,
                          Err* err) {
  if (input_conversion_value.type() == Value::NONE)
    return Value();  // Allow null inputs to mean discard the result.
  if (!input_conversion_value.VerifyTypeIs(Value::STRING, err))
    return Value();
  return DoConvertInputToValue(settings, input, origin, input_conversion_value,
                               input_conversion_value.string_value(), err);
}
