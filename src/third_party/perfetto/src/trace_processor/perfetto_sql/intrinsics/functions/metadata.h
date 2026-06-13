// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_FUNCTIONS_METADATA_H_
#define SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_FUNCTIONS_METADATA_H_

#include <optional>

#include "perfetto/base/status.h"
#include "src/trace_processor/sqlite/bindings/sqlite_function.h"
#include "src/trace_processor/sqlite/bindings/sqlite_value.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/metadata_tables_py.h"

namespace perfetto::trace_processor {

class PerfettoSqlEngine;

// extract_metadata(name) returns the "primary" metadata value for a given name.
struct ExtractMetadata : public sqlite::Function<ExtractMetadata> {
  static constexpr char kName[] = "extract_metadata";
  static constexpr int kArgCount = 1;

  struct Context {
    explicit Context(const TraceStorage* s)
        : storage(s),
          cursor(s->metadata_table().CreateCursor(
              {dataframe::FilterSpec{tables::MetadataTable::ColumnIndex::name,
                                     0, dataframe::Eq{}, std::nullopt}})) {}
    const TraceStorage* storage;
    tables::MetadataTable::ConstCursor cursor;
  };

  using UserData = Context;
  static void Step(sqlite3_context* ctx, int, sqlite3_value** argv);
};

// extract_metadata_for_machine(machine_id, name) returns the metadata value
// for a specific machine.
struct ExtractMetadataForMachine
    : public sqlite::Function<ExtractMetadataForMachine> {
  static constexpr char kName[] = "extract_metadata_for_machine";
  static constexpr int kArgCount = 2;

  struct Context {
    explicit Context(const TraceStorage* s)
        : storage(s),
          cursor(s->metadata_table().CreateCursor(
              {dataframe::FilterSpec{tables::MetadataTable::ColumnIndex::name,
                                     0, dataframe::Eq{}, std::nullopt},
               dataframe::FilterSpec{
                   tables::MetadataTable::ColumnIndex::machine_id, 1,
                   dataframe::Eq{}, std::nullopt}})) {}
    const TraceStorage* storage;
    tables::MetadataTable::ConstCursor cursor;
  };

  using UserData = Context;
  static void Step(sqlite3_context* ctx, int, sqlite3_value** argv);
};

// extract_metadata_for_trace(trace_id, name) returns the metadata value
// for a specific trace file.
struct ExtractMetadataForTrace
    : public sqlite::Function<ExtractMetadataForTrace> {
  static constexpr char kName[] = "extract_metadata_for_trace";
  static constexpr int kArgCount = 2;

  struct Context {
    explicit Context(const TraceStorage* s)
        : storage(s),
          cursor(s->metadata_table().CreateCursor(
              {dataframe::FilterSpec{tables::MetadataTable::ColumnIndex::name,
                                     0, dataframe::Eq{}, std::nullopt},
               dataframe::FilterSpec{
                   tables::MetadataTable::ColumnIndex::trace_id, 1,
                   dataframe::Eq{}, std::nullopt}})) {}
    const TraceStorage* storage;
    tables::MetadataTable::ConstCursor cursor;
  };

  using UserData = Context;
  static void Step(sqlite3_context* ctx, int, sqlite3_value** argv);
};

// extract_exact_metadata(machine_id, trace_id, name) returns the metadata value
// for a specific machine and trace file.
struct ExtractExactMetadata : public sqlite::Function<ExtractExactMetadata> {
  static constexpr char kName[] = "extract_exact_metadata";
  static constexpr int kArgCount = 3;

  struct Context {
    explicit Context(const TraceStorage* s)
        : storage(s),
          cursor(s->metadata_table().CreateCursor(
              {dataframe::FilterSpec{tables::MetadataTable::ColumnIndex::name,
                                     0, dataframe::Eq{}, std::nullopt},
               dataframe::FilterSpec{
                   tables::MetadataTable::ColumnIndex::machine_id, 1,
                   dataframe::Eq{}, std::nullopt},
               dataframe::FilterSpec{
                   tables::MetadataTable::ColumnIndex::trace_id, 2,
                   dataframe::Eq{}, std::nullopt}})) {}
    const TraceStorage* storage;
    tables::MetadataTable::ConstCursor cursor;
  };

  using UserData = Context;
  static void Step(sqlite3_context* ctx, int, sqlite3_value** argv);
};

base::Status RegisterMetadataFunctions(PerfettoSqlEngine& engine,
                                       TraceStorage* storage);

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_FUNCTIONS_METADATA_H_
