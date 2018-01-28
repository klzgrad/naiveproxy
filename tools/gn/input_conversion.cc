// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/input_conversion.h"

#include <utility>

#include "base/macros.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
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
  g_scheduler->input_file_manager()->AddDynamicInput(
      SourceFile(), &input_file, &tokens, &parse_root_ptr);

  input_file->SetContents(input);
  if (origin) {
    // This description will be the blame for any error messages caused by
    // script parsing or if a value is blamed. It will say
    // "Error at <...>:line:char" so here we try to make a string for <...>
    // that reads well in this context.
    input_file->set_friendly_name(
        "dynamically parsed input that " +
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

  std::unique_ptr<Scope> scope(new Scope(settings));
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

  *err = Err(original_input_conversion, "Not a valid input_conversion.",
             "Run gn help input_conversion to see your options.");
  return Value();
}

}  // namespace

extern const char kInputConversion_Help[] =
    R"(input_conversion: Specifies how to transform input to a variable.

  input_conversion is an argument to read_file and exec_script that specifies
  how the result of the read operation should be converted into a variable.

  "" (the default)
      Discard the result and return None.

  "list lines"
      Return the file contents as a list, with a string for each line. The
      newlines will not be present in the result. The last line may or may not
      end in a newline.

      After splitting, each individual line will be trimmed of whitespace on
      both ends.

  "scope"
      Execute the block as GN code and return a scope with the resulting values
      in it. If the input was:
        a = [ "hello.cc", "world.cc" ]
        b = 26
      and you read the result into a variable named "val", then you could
      access contents the "." operator on "val":
        sources = val.a
        some_count = val.b

  "string"
      Return the file contents into a single string.

  "value"
      Parse the input as if it was a literal rvalue in a buildfile. Examples of
      typical program output using this mode:
        [ "foo", "bar" ]     (result will be a list)
      or
        "foo bar"            (result will be a string)
      or
        5                    (result will be an integer)

      Note that if the input is empty, the result will be a null value which
      will produce an error if assigned to a variable.

  "trim ..."
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
