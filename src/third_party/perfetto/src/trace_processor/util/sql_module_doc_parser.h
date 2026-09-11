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

#ifndef SRC_TRACE_PROCESSOR_UTIL_SQL_MODULE_DOC_PARSER_H_
#define SRC_TRACE_PROCESSOR_UTIL_SQL_MODULE_DOC_PARSER_H_

#include <cstdint>
#include <string>
#include <vector>

namespace perfetto::trace_processor::stdlib_doc {

// Represents a column of a table/view or a return column of a table function.
struct Column {
  std::string name;
  std::string type;
  std::string description;
};

// Represents an argument to a function or macro.
// Intentionally identical fields to Column; kept as separate types for
// semantic clarity (a column of a table vs. a parameter of a function).
struct Arg {
  std::string name;
  std::string type;
  std::string description;
};

// Represents a table or view defined in a stdlib module.
struct TableOrView {
  std::string name;
  // "TABLE" or "VIEW".
  std::string type;
  std::string description;
  bool exposed = true;
  std::vector<Column> columns;
};

// Represents a scalar or table function defined in a stdlib module.
struct Function {
  std::string name;
  std::string description;
  bool exposed = true;
  bool is_table_function = false;
  std::string return_type;
  std::string return_description;
  std::vector<Arg> args;
  // Only populated for table functions.
  std::vector<Column> columns;
};

// Represents a macro defined in a stdlib module.
struct Macro {
  std::string name;
  std::string description;
  bool exposed = true;
  std::string return_type;
  std::string return_description;
  std::vector<Arg> args;
};

// Parsed documentation for a single stdlib SQL module file.
struct ParsedModule {
  std::vector<TableOrView> table_views;
  std::vector<Function> functions;
  std::vector<Macro> macros;
  // Parse errors encountered (non-fatal). Each entry describes what went wrong.
  std::vector<std::string> errors;
};

// Parses a single stdlib SQL module file and extracts documentation for all
// objects defined in it. Uses syntaqlite for SQL parsing and correlates
// comments with AST nodes to extract descriptions.
//
// Parse errors are non-fatal: a partially-populated ParsedModule is returned
// and each error is recorded in ParsedModule::errors.
// |sql| must remain valid for the duration of this call.
ParsedModule ParseStdlibModule(const char* sql, uint32_t sql_len);

}  // namespace perfetto::trace_processor::stdlib_doc

#endif  // SRC_TRACE_PROCESSOR_UTIL_SQL_MODULE_DOC_PARSER_H_
