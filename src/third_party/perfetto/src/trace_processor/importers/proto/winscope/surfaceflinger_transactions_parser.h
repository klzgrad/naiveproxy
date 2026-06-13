/*
 * Copyright (C) 2023 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_SURFACEFLINGER_TRANSACTIONS_PARSER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_SURFACEFLINGER_TRANSACTIONS_PARSER_H_

#include "src/trace_processor/tables/winscope_tables_py.h"
#include "src/trace_processor/util/descriptors.h"
#include "src/trace_processor/util/proto_to_args_parser.h"

namespace perfetto {

namespace trace_processor {

class TraceProcessorContext;

class SurfaceFlingerTransactionsParser {
 public:
  explicit SurfaceFlingerTransactionsParser(TraceProcessorContext*);
  void Parse(int64_t timestamp, protozero::ConstBytes);

 private:
  void ParseTransaction(int64_t timestamp,
                        protozero::ConstBytes transaction,
                        tables::SurfaceFlingerTransactionsTable::Id);

  void ParseAddedLayer(int64_t timestamp,
                       protozero::ConstBytes layer_creation_args,
                       tables::SurfaceFlingerTransactionsTable::Id);

  void AddNoopRow(tables::SurfaceFlingerTransactionsTable::Id snapshot_id,
                  uint64_t transaction_id,
                  int32_t pid,
                  int32_t uid);

  void AddLayerChangedRow(
      int64_t timestamp,
      protozero::ConstBytes layer_state,
      tables::SurfaceFlingerTransactionsTable::Id snapshot_id,
      uint64_t transaction_id,
      int32_t pid,
      int32_t uid,
      protozero::ConstBytes transaction);

  void ParseDisplayState(
      int64_t timestamp,
      protozero::ConstBytes display_state,
      tables::SurfaceFlingerTransactionsTable::Id snapshot_id,
      StringPool::Id transaction_type,
      std::optional<uint64_t> transaction_id,
      std::optional<int32_t> pid,
      std::optional<int32_t> uid,
      std::optional<protozero::ConstBytes> transaction);

  void AddArgs(int64_t timestamp,
               protozero::ConstBytes blob,
               tables::SurfaceFlingerTransactionTable::Id row_id,
               std::string message_type,
               std::optional<protozero::ConstBytes> transaction);

  std::vector<int32_t> DecodeFlags(uint32_t bitset,
                                   std::vector<int32_t> all_flags);

  void AddFlags(std::vector<std::string> flags, uint32_t flags_id);

  TraceProcessorContext* const context_;
  util::ProtoToArgsParser args_parser_;
  std::unordered_map<uint64_t, uint32_t> layer_flag_ids_;
  std::unordered_map<uint32_t, uint32_t> display_flag_ids_;
};
}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_SURFACEFLINGER_TRANSACTIONS_PARSER_H_
