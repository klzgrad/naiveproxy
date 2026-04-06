/*
 * Copyright (C) 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "src/protozero/text_to_proto/text_to_proto.h"

#include <cctype>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stack>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/protozero/message.h"
#include "perfetto/protozero/message_handle.h"
#include "perfetto/protozero/scattered_heap_buffer.h"

#include "protos/perfetto/common/descriptor.gen.h"

namespace protozero {

using perfetto::protos::gen::DescriptorProto;
using perfetto::protos::gen::EnumDescriptorProto;
using perfetto::protos::gen::EnumValueDescriptorProto;
using perfetto::protos::gen::FieldDescriptorProto;
using perfetto::protos::gen::FileDescriptorSet;

namespace {

constexpr bool IsOct(char c) {
  return (c >= '0' && c <= '7');
}

constexpr bool IsDigit(char c) {
  return (c >= '0' && c <= '9');
}

constexpr bool IsIdentifierStart(char c) {
  return ('A' <= c && c <= 'Z') || ('a' <= c && c <= 'z') || c == '_';
}

constexpr bool IsIdentifierBody(char c) {
  return IsIdentifierStart(c) || IsDigit(c);
}

const char* FieldToTypeName(const FieldDescriptorProto* field) {
  switch (field->type()) {
    case FieldDescriptorProto::TYPE_UINT64:
      return "uint64";
    case FieldDescriptorProto::TYPE_UINT32:
      return "uint32";
    case FieldDescriptorProto::TYPE_INT64:
      return "int64";
    case FieldDescriptorProto::TYPE_SINT64:
      return "sint64";
    case FieldDescriptorProto::TYPE_INT32:
      return "int32";
    case FieldDescriptorProto::TYPE_SINT32:
      return "sint32";
    case FieldDescriptorProto::TYPE_FIXED64:
      return "fixed64";
    case FieldDescriptorProto::TYPE_SFIXED64:
      return "sfixed64";
    case FieldDescriptorProto::TYPE_FIXED32:
      return "fixed32";
    case FieldDescriptorProto::TYPE_SFIXED32:
      return "sfixed32";
    case FieldDescriptorProto::TYPE_DOUBLE:
      return "double";
    case FieldDescriptorProto::TYPE_FLOAT:
      return "float";
    case FieldDescriptorProto::TYPE_BOOL:
      return "bool";
    case FieldDescriptorProto::TYPE_STRING:
      return "string";
    case FieldDescriptorProto::TYPE_BYTES:
      return "bytes";
    case FieldDescriptorProto::TYPE_GROUP:
      return "group";
    case FieldDescriptorProto::TYPE_MESSAGE:
      return "message";
    case FieldDescriptorProto::TYPE_ENUM:
      return "enum";
  }
  // For gcc
  PERFETTO_FATAL("Non complete switch");
}

std::string Format(const char* fmt,
                   const std::map<std::string, std::string>& args) {
  std::string result(fmt);
  for (const auto& key_value : args) {
    size_t start = result.find(key_value.first);
    PERFETTO_CHECK(start != std::string::npos);
    result.replace(start, key_value.first.size(), key_value.second);
    PERFETTO_CHECK(result.find(key_value.first) == std::string::npos);
  }
  return result;
}

enum ParseState {
  kWaitingForKey,
  kReadingKey,
  kWaitingForValue,
  kReadingStringValue,
  kReadingStringEscape,
  kReadingNumericValue,
  kReadingIdentifierValue,
};

struct Token {
  size_t offset;
  size_t column;
  size_t row;
  perfetto::base::StringView txt;

  size_t size() const { return txt.size(); }
  std::string ToStdString() const { return txt.ToStdString(); }
};

struct ParserDelegateContext {
  const DescriptorProto* descriptor;
  protozero::Message* message;
  std::set<std::string> seen_fields;
};

class ErrorReporter {
 public:
  ErrorReporter(std::string file_name, std::string_view config)
      : file_name_(std::move(file_name)), config_(config) {}

  void AddError(size_t row,
                size_t column,
                size_t length,
                const std::string& message) {
    // Protobuf uses 1-indexed for row and column. Although in some rare cases
    // they can be 0 if it can't locate the error.
    row = row > 0 ? row - 1 : 0;
    column = column > 0 ? column - 1 : 0;
    parsed_successfully_ = false;
    std::string line = ExtractLine(row).ToStdString();
    if (!line.empty() && line[line.length() - 1] == '\n') {
      line.erase(line.length() - 1);
    }

    std::string guide(column + length, ' ');
    for (size_t i = column; i < column + length; i++) {
      guide[i] = i == column ? '^' : '~';
    }
    error_ += file_name_ + ":" + std::to_string(row + 1) + ":" +
              std::to_string(column + 1) + " error: " + message + "\n";
    error_ += line + "\n";
    error_ += guide + "\n";
  }

  bool success() const { return parsed_successfully_; }
  const std::string& error() const { return error_; }

 private:
  perfetto::base::StringView ExtractLine(size_t line) {
    const char* start = config_.data();
    const char* end = config_.data();

    for (size_t i = 0; i < line + 1; i++) {
      start = end;
      char c;
      while ((c = *end++) && c != '\n')
        ;
    }
    return {start, static_cast<size_t>(end - start)};
  }

  bool parsed_successfully_ = true;
  std::string file_name_;
  std::string error_;
  std::string_view config_;
};

class ParserDelegate {
 public:
  ParserDelegate(
      const DescriptorProto* descriptor,
      protozero::Message* message,
      ErrorReporter* reporter,
      std::map<std::string, const DescriptorProto*> name_to_descriptor,
      std::map<std::string, const EnumDescriptorProto*> name_to_enum)
      : reporter_(reporter),
        name_to_descriptor_(std::move(name_to_descriptor)),
        name_to_enum_(std::move(name_to_enum)) {
    ctx_.push(ParserDelegateContext{descriptor, message, {}});
  }

  void NumericField(const Token& key, const Token& value) {
    const FieldDescriptorProto* field =
        FindFieldByName(key, value,
                        {
                            FieldDescriptorProto::TYPE_UINT64,
                            FieldDescriptorProto::TYPE_UINT32,
                            FieldDescriptorProto::TYPE_INT64,
                            FieldDescriptorProto::TYPE_SINT64,
                            FieldDescriptorProto::TYPE_INT32,
                            FieldDescriptorProto::TYPE_SINT32,
                            FieldDescriptorProto::TYPE_FIXED64,
                            FieldDescriptorProto::TYPE_SFIXED64,
                            FieldDescriptorProto::TYPE_FIXED32,
                            FieldDescriptorProto::TYPE_SFIXED32,
                            FieldDescriptorProto::TYPE_DOUBLE,
                            FieldDescriptorProto::TYPE_FLOAT,
                        });
    if (!field)
      return;
    const auto& field_type = field->type();
    switch (field_type) {
      case FieldDescriptorProto::TYPE_UINT64:
        return VarIntField<uint64_t>(field, value);
      case FieldDescriptorProto::TYPE_UINT32:
        return VarIntField<uint32_t>(field, value);
      case FieldDescriptorProto::TYPE_INT64:
      case FieldDescriptorProto::TYPE_SINT64:
        return VarIntField<int64_t>(field, value);
      case FieldDescriptorProto::TYPE_INT32:
      case FieldDescriptorProto::TYPE_SINT32:
        return VarIntField<int32_t>(field, value);

      case FieldDescriptorProto::TYPE_FIXED64:
      case FieldDescriptorProto::TYPE_SFIXED64:
        return FixedField<int64_t>(field, value);

      case FieldDescriptorProto::TYPE_FIXED32:
      case FieldDescriptorProto::TYPE_SFIXED32:
        return FixedField<int32_t>(field, value);

      case FieldDescriptorProto::TYPE_DOUBLE:
        return FixedFloatField<double>(field, value);
      case FieldDescriptorProto::TYPE_FLOAT:
        return FixedFloatField<float>(field, value);

      case FieldDescriptorProto::TYPE_BOOL:
      case FieldDescriptorProto::TYPE_STRING:
      case FieldDescriptorProto::TYPE_BYTES:
      case FieldDescriptorProto::TYPE_GROUP:
      case FieldDescriptorProto::TYPE_MESSAGE:
      case FieldDescriptorProto::TYPE_ENUM:
        PERFETTO_FATAL("Invalid type");
    }
  }

  void StringField(const Token& key, const Token& value) {
    const FieldDescriptorProto* field =
        FindFieldByName(key, value,
                        {
                            FieldDescriptorProto::TYPE_STRING,
                            FieldDescriptorProto::TYPE_BYTES,
                        });
    if (!field)
      return;
    auto field_id = static_cast<uint32_t>(field->number());
    const auto& field_type = field->type();
    PERFETTO_CHECK(field_type == FieldDescriptorProto::TYPE_STRING ||
                   field_type == FieldDescriptorProto::TYPE_BYTES);

    std::unique_ptr<char, perfetto::base::FreeDeleter> s(
        static_cast<char*>(malloc(value.size())));
    size_t j = 0;
    const char* const txt = value.txt.data();
    for (size_t i = 0; i < value.size(); i++) {
      char c = txt[i];
      if (c == '\\') {
        if (i + 1 >= value.size()) {
          // This should be caught by the lexer.
          PERFETTO_FATAL("Escape at end of string.");
          return;
        }
        char next = txt[++i];
        switch (next) {
          case '\\':
          case '\'':
          case '"':
          case '?':
            s.get()[j++] = next;
            break;
          case 'a':
            s.get()[j++] = '\a';
            break;
          case 'b':
            s.get()[j++] = '\b';
            break;
          case 'f':
            s.get()[j++] = '\f';
            break;
          case 'n':
            s.get()[j++] = '\n';
            break;
          case 'r':
            s.get()[j++] = '\r';
            break;
          case 't':
            s.get()[j++] = '\t';
            break;
          case 'v':
            s.get()[j++] = '\v';
            break;
          case '0':
          case '1':
          case '2':
          case '3':
          case '4':
          case '5':
          case '6':
          case '7':
          case '8':
          case '9': {
            // Cases 8 and 9 are not really required and are only added for the
            // sake of error reporting.
            bool oct_err = false;
            if (i + 2 >= value.size() || !IsOct(txt[i + 1]) ||
                !IsOct(txt[i + 2])) {
              oct_err = true;
            } else {
              char buf[4]{next, txt[++i], txt[++i], '\0'};
              auto octval = perfetto::base::CStringToUInt32(buf, 8);
              if (!octval.has_value() || *octval > 0xff) {
                oct_err = true;
              } else {
                s.get()[j++] = static_cast<char>(static_cast<uint8_t>(*octval));
              }
            }
            if (oct_err) {
              AddError(value,
                       "Malformed string escape in $k in proto $n on '$v'. "
                       "\\NNN escapes must be exactly three octal digits <= "
                       "\\377 (0xff).",
                       std::map<std::string, std::string>{
                           {"$k", key.ToStdString()},
                           {"$n", descriptor_name()},
                           {"$v", value.ToStdString()},
                       });
            }
            break;
          }
          default:
            AddError(value,
                     "Unknown string escape in $k in "
                     "proto $n: '$v'",
                     std::map<std::string, std::string>{
                         {"$k", key.ToStdString()},
                         {"$n", descriptor_name()},
                         {"$v", value.ToStdString()},
                     });
            return;
        }
      } else {
        s.get()[j++] = c;
      }
    }
    msg()->AppendBytes(field_id, s.get(), j);
  }

  void IdentifierField(const Token& key, const Token& value) {
    const FieldDescriptorProto* field =
        FindFieldByName(key, value,
                        {
                            FieldDescriptorProto::TYPE_BOOL,
                            FieldDescriptorProto::TYPE_ENUM,
                        });
    if (!field)
      return;
    uint32_t field_id = static_cast<uint32_t>(field->number());
    const auto& field_type = field->type();
    if (field_type == FieldDescriptorProto::TYPE_BOOL) {
      if (value.txt != "true" && value.txt != "false") {
        AddError(value,
                 "Expected 'true' or 'false' for boolean field $k in "
                 "proto $n instead saw '$v'",
                 std::map<std::string, std::string>{
                     {"$k", key.ToStdString()},
                     {"$n", descriptor_name()},
                     {"$v", value.ToStdString()},
                 });
        return;
      }
      msg()->AppendTinyVarInt(field_id, value.txt == "true" ? 1 : 0);
    } else if (field_type == FieldDescriptorProto::TYPE_ENUM) {
      const std::string& type_name = field->type_name();
      const EnumDescriptorProto* enum_descriptor = name_to_enum_[type_name];
      PERFETTO_CHECK(enum_descriptor);
      bool found_value = false;
      int32_t enum_value_number = 0;
      for (const EnumValueDescriptorProto& enum_value :
           enum_descriptor->value()) {
        if (value.ToStdString() != enum_value.name())
          continue;
        found_value = true;
        enum_value_number = enum_value.number();
        break;
      }
      if (!found_value) {
        AddError(value,
                 "Unexpected value '$v' for enum field $k in "
                 "proto $n",
                 std::map<std::string, std::string>{
                     {"$v", value.ToStdString()},
                     {"$k", key.ToStdString()},
                     {"$n", descriptor_name()},
                 });
        return;
      }
      msg()->AppendVarInt<int32_t>(field_id, enum_value_number);
    }
  }

  bool BeginNestedMessage(const Token& key, const Token& value) {
    const FieldDescriptorProto* field =
        FindFieldByName(key, value,
                        {
                            FieldDescriptorProto::TYPE_MESSAGE,
                        });
    if (!field) {
      // FindFieldByName adds an error.
      return false;
    }
    uint32_t field_id = static_cast<uint32_t>(field->number());
    const std::string& type_name = field->type_name();
    const DescriptorProto* nested_descriptor = name_to_descriptor_[type_name];
    PERFETTO_CHECK(nested_descriptor);
    auto* nested_msg = msg()->BeginNestedMessage<protozero::Message>(field_id);
    ctx_.push(ParserDelegateContext{nested_descriptor, nested_msg, {}});
    return true;
  }

  void EndNestedMessage() {
    msg()->Finalize();
    ctx_.pop();
  }

  void Eof() {}

  void AddError(size_t row,
                size_t column,
                const char* fmt,
                const std::map<std::string, std::string>& args) {
    reporter_->AddError(row, column, 0, Format(fmt, args));
  }

  void AddError(const Token& token,
                const char* fmt,
                const std::map<std::string, std::string>& args) {
    reporter_->AddError(token.row, token.column, token.size(),
                        Format(fmt, args));
  }

 private:
  template <typename T>
  void VarIntField(const FieldDescriptorProto* field, const Token& t) {
    auto field_id = static_cast<uint32_t>(field->number());
    uint64_t n = 0;
    PERFETTO_CHECK(ParseInteger(t.txt, &n));
    if (field->type() == FieldDescriptorProto::TYPE_SINT64 ||
        field->type() == FieldDescriptorProto::TYPE_SINT32) {
      msg()->AppendSignedVarInt<T>(field_id, static_cast<T>(n));
    } else {
      msg()->AppendVarInt<T>(field_id, static_cast<T>(n));
    }
  }

  template <typename T>
  void FixedField(const FieldDescriptorProto* field, const Token& t) {
    uint32_t field_id = static_cast<uint32_t>(field->number());
    uint64_t n = 0;
    PERFETTO_CHECK(ParseInteger(t.txt, &n));
    msg()->AppendFixed<T>(field_id, static_cast<T>(n));
  }

  template <typename T>
  void FixedFloatField(const FieldDescriptorProto* field, const Token& t) {
    uint32_t field_id = static_cast<uint32_t>(field->number());
    std::optional<double> opt_n =
        perfetto::base::StringToDouble(t.ToStdString());
    msg()->AppendFixed<T>(field_id, static_cast<T>(opt_n.value_or(0l)));
  }

  template <typename T>
  bool ParseInteger(perfetto::base::StringView s, T* number_ptr) {
    uint64_t n = 0;
    PERFETTO_CHECK(sscanf(s.ToStdString().c_str(), "%" PRIu64, &n) == 1);
    PERFETTO_CHECK(n <= std::numeric_limits<T>::max());
    *number_ptr = static_cast<T>(n);
    return true;
  }

  const FieldDescriptorProto* FindFieldByName(
      const Token& key,
      const Token& value,
      const std::set<FieldDescriptorProto::Type>& valid_field_types) {
    const std::string field_name = key.ToStdString();
    const FieldDescriptorProto* field_descriptor = nullptr;
    for (const auto& f : descriptor()->field()) {
      if (f.name() == field_name) {
        field_descriptor = &f;
        break;
      }
    }

    if (!field_descriptor) {
      AddError(key, "No field named \"$n\" in proto $p",
               {
                   {"$n", field_name},
                   {"$p", descriptor_name()},
               });
      return nullptr;
    }

    bool is_repeated =
        field_descriptor->label() == FieldDescriptorProto::LABEL_REPEATED;
    auto it_and_inserted = ctx_.top().seen_fields.emplace(field_name);
    if (!it_and_inserted.second && !is_repeated) {
      AddError(key, "Saw non-repeating field '$f' more than once",
               {
                   {"$f", field_name},
               });
    }

    if (!valid_field_types.count(field_descriptor->type())) {
      AddError(value,
               "Expected value of type $t for field $k in proto $n "
               "instead saw '$v'",
               {
                   {"$t", FieldToTypeName(field_descriptor)},
                   {"$k", field_name},
                   {"$n", descriptor_name()},
                   {"$v", value.ToStdString()},
               });
      return nullptr;
    }

    return field_descriptor;
  }

  const DescriptorProto* descriptor() {
    PERFETTO_CHECK(!ctx_.empty());
    return ctx_.top().descriptor;
  }

  const std::string& descriptor_name() { return descriptor()->name(); }

  protozero::Message* msg() {
    PERFETTO_CHECK(!ctx_.empty());
    return ctx_.top().message;
  }

  std::stack<ParserDelegateContext> ctx_;
  ErrorReporter* reporter_;
  std::map<std::string, const DescriptorProto*> name_to_descriptor_;
  std::map<std::string, const EnumDescriptorProto*> name_to_enum_;
};

void Parse(std::string_view input, ParserDelegate* delegate) {
  ParseState state = kWaitingForKey;
  size_t column = 0;
  size_t row = 1;
  size_t depth = 0;
  bool saw_colon_for_this_key = false;
  bool saw_semicolon_for_this_value = true;
  bool comment_till_eol = false;
  Token key{};
  Token value{};

  for (size_t i = 0; i < input.size(); i++, column++) {
    bool last_character = i + 1 == input.size();
    char c = input.at(i);
    if (c == '\n') {
      column = 0;
      row++;
      if (comment_till_eol) {
        comment_till_eol = false;
        continue;
      }
    }
    if (comment_till_eol)
      continue;

    switch (state) {
      case kWaitingForKey:
        if (isspace(c))
          continue;
        if (c == '#') {
          comment_till_eol = true;
          continue;
        }
        if (c == '}') {
          if (depth == 0) {
            delegate->AddError(row, column, "Unmatched closing brace", {});
            return;
          }
          saw_semicolon_for_this_value = false;
          depth--;
          delegate->EndNestedMessage();
          continue;
        }
        if (!saw_semicolon_for_this_value && c == ';') {
          saw_semicolon_for_this_value = true;
          continue;
        }
        if (IsIdentifierStart(c)) {
          saw_colon_for_this_key = false;
          state = kReadingKey;
          key.offset = i;
          key.row = row;
          key.column = column;
          continue;
        }
        break;

      case kReadingKey:
        if (IsIdentifierBody(c))
          continue;
        key.txt = perfetto::base::StringView(input.data() + key.offset,
                                             i - key.offset);
        state = kWaitingForValue;
        if (c == '#')
          comment_till_eol = true;
        continue;

      case kWaitingForValue:
        if (isspace(c))
          continue;
        if (c == '#') {
          comment_till_eol = true;
          continue;
        }
        value.offset = i;
        value.row = row;
        value.column = column;

        if (c == ':' && !saw_colon_for_this_key) {
          saw_colon_for_this_key = true;
          continue;
        }
        if (c == '"') {
          state = kReadingStringValue;
          continue;
        }
        if (c == '-' || IsDigit(c) || c == '.') {
          state = kReadingNumericValue;
          continue;
        }
        if (IsIdentifierStart(c)) {
          state = kReadingIdentifierValue;
          continue;
        }
        if (c == '{') {
          state = kWaitingForKey;
          depth++;
          value.txt =
              perfetto::base::StringView(input.data() + value.offset, 1);
          if (!delegate->BeginNestedMessage(key, value)) {
            return;
          }
          continue;
        }
        break;

      case kReadingNumericValue:
        if (isspace(c) || c == ';' || last_character) {
          bool keep_last = last_character && !isspace(c) && c != ';';
          size_t size = i - value.offset + (keep_last ? 1 : 0);
          value.txt =
              perfetto::base::StringView(input.data() + value.offset, size);
          saw_semicolon_for_this_value = c == ';';
          state = kWaitingForKey;
          delegate->NumericField(key, value);
          continue;
        }
        if (IsDigit(c) || c == '.')
          continue;
        break;

      case kReadingStringValue:
        if (c == '\\') {
          state = kReadingStringEscape;
        } else if (c == '"') {
          size_t size = i - value.offset - 1;
          value.column++;
          value.txt =
              perfetto::base::StringView(input.data() + value.offset + 1, size);
          saw_semicolon_for_this_value = false;
          state = kWaitingForKey;
          delegate->StringField(key, value);
        }
        continue;

      case kReadingStringEscape:
        state = kReadingStringValue;
        continue;

      case kReadingIdentifierValue:
        if (isspace(c) || c == ';' || c == '#' || last_character) {
          bool keep_last =
              last_character && !isspace(c) && c != ';' && c != '#';
          size_t size = i - value.offset + (keep_last ? 1 : 0);
          value.txt =
              perfetto::base::StringView(input.data() + value.offset, size);
          comment_till_eol = c == '#';
          saw_semicolon_for_this_value = c == ';';
          state = kWaitingForKey;
          delegate->IdentifierField(key, value);
          continue;
        }
        if (IsIdentifierBody(c)) {
          continue;
        }
        break;
    }
    delegate->AddError(row, column, "Unexpected character '$c'",
                       std::map<std::string, std::string>{
                           {"$c", std::string(1, c)},
                       });
    return;
  }  // for
  if (depth > 0)
    delegate->AddError(row, column, "Nested message not closed", {});
  if (state != kWaitingForKey)
    delegate->AddError(row, column, "Unexpected end of input", {});
  delegate->Eof();
}

void AddNestedDescriptors(
    const std::string& prefix,
    const DescriptorProto* descriptor,
    std::map<std::string, const DescriptorProto*>* name_to_descriptor,
    std::map<std::string, const EnumDescriptorProto*>* name_to_enum) {
  for (const EnumDescriptorProto& enum_descriptor : descriptor->enum_type()) {
    const std::string name = prefix + "." + enum_descriptor.name();
    (*name_to_enum)[name] = &enum_descriptor;
  }
  for (const DescriptorProto& nested_descriptor : descriptor->nested_type()) {
    const std::string name = prefix + "." + nested_descriptor.name();
    (*name_to_descriptor)[name] = &nested_descriptor;
    AddNestedDescriptors(name, &nested_descriptor, name_to_descriptor,
                         name_to_enum);
  }
}

}  // namespace

perfetto::base::StatusOr<std::vector<uint8_t>> TextToProto(
    const uint8_t* descriptor_set_ptr,
    size_t descriptor_set_size,
    const std::string& root_type,
    const std::string& file_name,
    std::string_view input) {
  std::map<std::string, const DescriptorProto*> name_to_descriptor;
  std::map<std::string, const EnumDescriptorProto*> name_to_enum;
  FileDescriptorSet file_descriptor_set;

  {
    file_descriptor_set.ParseFromArray(descriptor_set_ptr, descriptor_set_size);
    for (const auto& file_descriptor : file_descriptor_set.file()) {
      for (const auto& enum_descriptor : file_descriptor.enum_type()) {
        const std::string name =
            "." + file_descriptor.package() + "." + enum_descriptor.name();
        name_to_enum[name] = &enum_descriptor;
      }
      for (const auto& descriptor : file_descriptor.message_type()) {
        const std::string name =
            "." + file_descriptor.package() + "." + descriptor.name();
        name_to_descriptor[name] = &descriptor;
        AddNestedDescriptors(name, &descriptor, &name_to_descriptor,
                             &name_to_enum);
      }
    }
  }

  const DescriptorProto* descriptor = name_to_descriptor[root_type];
  PERFETTO_CHECK(descriptor);

  protozero::HeapBuffered<protozero::Message> message;
  ErrorReporter reporter(file_name, input);
  ParserDelegate delegate(descriptor, message.get(), &reporter,
                          std::move(name_to_descriptor),
                          std::move(name_to_enum));
  Parse(input, &delegate);
  if (!reporter.success())
    return perfetto::base::ErrStatus("%s", reporter.error().c_str());
  return message.SerializeAsArray();
}

}  // namespace protozero
