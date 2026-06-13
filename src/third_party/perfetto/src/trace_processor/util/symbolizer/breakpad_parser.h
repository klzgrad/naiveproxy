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

#ifndef SRC_TRACE_PROCESSOR_UTIL_SYMBOLIZER_BREAKPAD_PARSER_H_
#define SRC_TRACE_PROCESSOR_UTIL_SYMBOLIZER_BREAKPAD_PARSER_H_

#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/string_view.h"

namespace perfetto {
namespace profiling {

// The BreakpadParser class is used to parse a breakpad file and store data on
// symbols so that a given address can be used to query a symbol. The class is
// instantiated with the |file_path| of the file to be parsed. Breakpad file
// format:
// https://chromium.googlesource.com/breakpad/breakpad/+/master/docs/symbol_files.md
// Usage:
//
// BreakpadParser parser("file.breakpad");
// parser.ParseFile();
// std::string symbol = parser.GetSymbol(addr);
class BreakpadParser {
 public:
  struct Symbol {
    // The address where a function starts.
    uint64_t start_address = 0;
    // The length in bytes of the function's instructions.
    size_t function_size = 0;
    // The human-readable name for the function signature.
    std::string symbol_name;
  };

  // Represents a line record that maps a range of machine code addresses to
  // source file information (file number and line number).
  struct LineRecord {
    // The starting address of the machine code range.
    uint64_t start_address = 0;
    // The size in bytes of the machine code range.
    size_t size = 0;
    // The source line number.
    uint32_t line = 0;
    // The file number that references an entry in the FILE records.
    uint32_t file_number = 0;
  };

  // Supported record types for the Breakpad symbol file format.
  // https://chromium.googlesource.com/breakpad/breakpad/+/HEAD/docs/symbol_files.md
  enum class RecordType {
    FUNC,   // FUNC [m] address size parameter_size name
    PUBLIC  // PUBLIC [m] address parameter_size name
  };

  explicit BreakpadParser(const std::string& file_path);

  BreakpadParser(const BreakpadParser& other) = delete;
  BreakpadParser& operator=(const BreakpadParser& other) = delete;

  // Fills in |symbols_| by parsing the breakpad file given.
  // Returns true if it is able to parse the entire file specified by
  // |file_path_|. Returns false if the parsing fails.
  bool ParseFile();

  // Parses from  a string instead of a file.
  bool ParseFromString(const std::string& file_contents);

  // Returns the function name corresponding to |address| as a string. The
  // search is log(N) on the number of functions in the binary. |address| is the
  // relative offset from the start of the binary.
  std::optional<std::string> GetSymbol(uint64_t address) const;

  // As same as GetSymbol, but retrieve from the PUBLIC records.
  std::optional<std::string> GetPublicSymbol(uint64_t address) const;

  // Returns source file name and line number for the corresponding code address
  std::optional<std::tuple<std::string, uint32_t>> GetSourceLocation(
      uint64_t address) const;

  const std::vector<Symbol>& symbols_for_testing() const { return symbols_; }
  const std::vector<Symbol>& public_symbols_for_testing() const {
    return public_symbols_;
  }

 private:
  // Parses the given string and creates a new Symbol object if it is a
  // {RecordType} record. Returns an ok status if it was successfully able to
  // add to the |symbols_ or public_symbols_| or if the string is not a FUNC
  // record. Return a fail status on parsing errors on FUNC record.
  base::Status ParseIfRecord(base::StringView current_line, RecordType type);

  base::Status ParseFileRecord(base::StringView current_line);

  base::Status ParseLineRecord(base::StringView current_line);

  std::string GetRecordLabel(RecordType type) const;
  void StoreSymbol(Symbol& symbol, RecordType type);

  std::vector<Symbol> symbols_;
  std::vector<Symbol> public_symbols_;
  std::vector<LineRecord> line_records_;
  std::unordered_map<uint32_t, std::string> source_files_;
  const std::string file_path_;
};

}  // namespace profiling
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_UTIL_SYMBOLIZER_BREAKPAD_PARSER_H_
