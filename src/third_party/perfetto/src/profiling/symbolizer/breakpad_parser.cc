/*
 * Copyright (C) 2021 The Android Open Source Project
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
#include <memory>

#include "src/profiling/symbolizer/breakpad_parser.h"

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/fixed_string_writer.h"
#include "perfetto/ext/base/string_splitter.h"
#include "perfetto/ext/base/string_utils.h"

namespace perfetto {
namespace profiling {

namespace {

bool SymbolComparator(const uint64_t i, const BreakpadParser::Symbol& sym) {
  return i < sym.start_address;
}

std::optional<std::string> GetFileContents(const std::string& file_path) {
  std::string file_contents;
  base::ScopedFile fd = base::OpenFile(file_path, O_RDONLY);
  // Read the contents of the file into |file_contents|.
  if (!fd) {
    return std::nullopt;
  }
  if (!base::ReadFileDescriptor(fd.get(), &file_contents)) {
    return std::nullopt;
  }

  return std::make_optional(std::move(file_contents));
}

// Parses the given string and determines if it begins with the label
// 'MODULE'. Returns an ok status if it does begin with this label and a fail
// status otherwise.
base::Status ParseIfModuleRecord(base::StringView first_line) {
  // Split the given line by spaces.
  const char kModuleLabel[] = "MODULE";
  // Check to see if the line starts with 'MODULE'.
  if (!base::StartsWith(first_line.ToStdString(), kModuleLabel)) {
    return base::Status("Breakpad file not formatted correctly.");
  }
  return base::OkStatus();
}

}  // namespace

BreakpadParser::BreakpadParser(const std::string& file_path)
    : file_path_(file_path) {}

bool BreakpadParser::ParseFile() {
  std::optional<std::string> file_contents = GetFileContents(file_path_);
  if (!file_contents) {
    PERFETTO_ELOG("Could not get file contents of %s.", file_path_.c_str());
    return false;
  }

  // TODO(uwemwilson): Extract a build id and store it in the Symbol object.

  if (!ParseFromString(*file_contents)) {
    PERFETTO_ELOG("Could not parse file contents.");
    return false;
  }

  return true;
}

bool BreakpadParser::ParseFromString(const std::string& file_contents) {
  // Create StringSplitter objects for each line so that specific lines can be
  // used to create StringSplitter objects for words on that line.
  base::StringSplitter lines(file_contents, '\n');
  if (!lines.Next()) {
    // File must be empty, so just return true and continue.
    return true;
  }

  // TODO(crbug/1239750): Extract a build id and store it in the Symbol object.
  base::StringView first_line(lines.cur_token(), lines.cur_token_size());
  base::Status parse_record_status = ParseIfModuleRecord(first_line);
  if (!parse_record_status.ok()) {
    PERFETTO_ELOG("%s Breakpad files should begin with a MODULE record",
                  parse_record_status.message().c_str());
    return false;
  }

  // Parse each line.
  while (lines.Next()) {
    parse_record_status = ParseIfRecord(lines.cur_token(), RecordType::FUNC);
    if (!parse_record_status.ok()) {
      PERFETTO_ELOG("%s", parse_record_status.message().c_str());
      return false;
    }

    parse_record_status = ParseIfRecord(lines.cur_token(), RecordType::PUBLIC);
    if (!parse_record_status.ok()) {
      PERFETTO_ELOG("%s", parse_record_status.message().c_str());
      return false;
    }
  }

  return true;
}

std::optional<std::string> BreakpadParser::GetSymbol(uint64_t address) const {
  // Returns an iterator pointing to the first element where the symbol's start
  // address is greater than |address|.
  auto it = std::upper_bound(symbols_.begin(), symbols_.end(), address,
                             &SymbolComparator);
  // If the first symbol's address is greater than |address| then |address| is
  // too low to appear in |symbols_|.
  if (it == symbols_.begin()) {
    return std::nullopt;
  }
  // upper_bound() returns the first symbol who's start address is greater than
  // |address|. Therefore to find the symbol with a range of addresses that
  // |address| falls into, we check the previous symbol.
  it--;
  // Check to see if the address is in the function's range.
  if (address >= it->start_address &&
      address < it->start_address + it->function_size) {
    return it->symbol_name;
  }
  return std::nullopt;
}

std::optional<std::string> BreakpadParser::GetPublicSymbol(
    uint64_t address) const {
  // Returns an iterator pointing to the first element where the symbol's start
  // address is greater than |address|.
  auto it = std::upper_bound(public_symbols_.begin(), public_symbols_.end(),
                             address, &SymbolComparator);
  // If the first symbol's address is greater than |address| then |address| is
  // too low to appear in |public_symbols_|.
  if (it == public_symbols_.begin() || it == public_symbols_.end()) {
    return std::nullopt;
  }
  it--;
  // Check to see if the address is in the function's range, since the PUBLIC
  // record only store the parameter size, but not the function size. we use
  // 0xffff to do the sanity check.
  if (address >= it->start_address && address < it->start_address + 0xFFFF) {
    return it->symbol_name;
  }
  return std::nullopt;
}

std::string BreakpadParser::GetRecordLabel(RecordType type) const {
  switch (type) {
    case RecordType::FUNC:
      return "FUNC";
    case RecordType::PUBLIC:
      return "PUBLIC";
  }
  PERFETTO_FATAL("For GCC");
}

void BreakpadParser::StoreSymbol(Symbol& symbol, RecordType type) {
  switch (type) {
    case RecordType::FUNC:
      symbols_.emplace_back(std::move(symbol));
      break;
    case RecordType::PUBLIC:
      public_symbols_.emplace_back(std::move(symbol));
      break;
  }
}

base::Status BreakpadParser::ParseIfRecord(base::StringView current_line,
                                           const RecordType type) {
  // The parser currently supports FUNC and PUBLIC records.
  // FUNC   [m] address size parameter_size name
  // PUBLIC [m] address      parameter_size name
  const std::string typeLabel = GetRecordLabel(type);
  base::StringSplitter words(current_line.ToStdString(), ' ');

  // Check to see if the first word indicates a {RecordType} record. If it is,
  // create a Symbol struct and add tokens from words. If it isn't the function
  // can just return true and resume parsing file.
  if (!words.Next() || strcmp(words.cur_token(), typeLabel.c_str()) != 0) {
    return base::OkStatus();
  }

  Symbol new_symbol;
  // The second token, 'm' is optional.
  const char kOptionalArg[] = "m";
  // Get the first argument on the line.
  words.Next();

  // If the optional argument is present, skip to the next token.
  if (strcmp(words.cur_token(), kOptionalArg) == 0) {
    words.Next();
  }

  // Get the start address.
  std::optional<uint64_t> optional_address =
      base::CStringToUInt64(words.cur_token(), 16);
  if (!optional_address) {
    return base::Status("Address should be hexadecimal.");
  }
  new_symbol.start_address = *optional_address;

  // The function size is only stored in the FUNC record, so only parse it if
  // the record type is FUNC.
  if (type == RecordType::FUNC) {
    // Get the function size.
    words.Next();
    std::optional<size_t> optional_func_size =
        base::CStringToUInt32(words.cur_token(), 16);
    if (!optional_func_size) {
      return base::Status("Function size should be hexadecimal.");
    }
    new_symbol.function_size = *optional_func_size;
  }

  // Skip the parameter size.
  words.Next();

  // Get the function name. Function names can have spaces, so any token is now
  // considered a part of the function name and will be appended to the buffer
  // in |func_name_writer|.
  std::unique_ptr<char[]> joined_string(new char[current_line.size()]);
  base::FixedStringWriter func_name_writer(joined_string.get(),
                                           current_line.size());
  bool first_token = true;
  while (words.Next()) {
    if (!first_token) {
      func_name_writer.AppendChar(' ');
    } else {
      first_token = false;
    }
    func_name_writer.AppendString(words.cur_token(), strlen(words.cur_token()));
  }

  new_symbol.symbol_name = func_name_writer.GetStringView().ToStdString();

  StoreSymbol(new_symbol, type);

  return base::OkStatus();
}

}  // namespace profiling
}  // namespace perfetto
