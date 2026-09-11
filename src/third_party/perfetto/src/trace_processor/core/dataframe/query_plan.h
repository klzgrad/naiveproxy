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

#ifndef SRC_TRACE_PROCESSOR_CORE_DATAFRAME_QUERY_PLAN_H_
#define SRC_TRACE_PROCESSOR_CORE_DATAFRAME_QUERY_PLAN_H_

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/base64.h"
#include "perfetto/ext/base/small_vector.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/public/compiler.h"
#include "src/trace_processor/core/dataframe/dataframe_register_cache.h"
#include "src/trace_processor/core/dataframe/specs.h"
#include "src/trace_processor/core/dataframe/types.h"
#include "src/trace_processor/core/interpreter/bytecode_builder.h"
#include "src/trace_processor/core/interpreter/bytecode_core.h"
#include "src/trace_processor/core/interpreter/bytecode_registers.h"
#include "src/trace_processor/core/interpreter/interpreter_types.h"
#include "src/trace_processor/core/util/bit_vector.h"
#include "src/trace_processor/core/util/range.h"
#include "src/trace_processor/core/util/slab.h"
#include "src/trace_processor/core/util/span.h"
#include "src/trace_processor/core/util/type_set.h"

namespace perfetto::trace_processor::core::dataframe {

class Dataframe;

// Specification for initializing a register before bytecode execution.
// The plan contains abstract references (column indices, index IDs), and
// the cursor converts these to concrete pointers based on the kind.
struct RegisterInit {
  struct NullBitvector {};
  struct IndexVector {};
  struct SmallValueEqBitvector {};
  struct SmallValueEqPopcount {};

  using Type = TypeSet<Id,
                       Uint32,
                       Int32,
                       Int64,
                       Double,
                       String,
                       NullBitvector,
                       IndexVector,
                       SmallValueEqBitvector,
                       SmallValueEqPopcount>;
  uint32_t dest_register = 0;
  Type kind{Id{}};
  uint16_t source_index = 0;  // col_index or index_id depending on kind
  uint16_t pad_ = 0;          // Explicit trailing padding
};
static_assert(std::is_trivially_copyable_v<RegisterInit>);

// A QueryPlan encapsulates all the information needed to execute a query,
// including the bytecode instructions and interpreter configuration.
struct QueryPlanImpl {
  // Contains various parameters required for execution of this query plan.
  struct ExecutionParams {
    // An estimate for the cost of executing the query plan.
    double estimated_cost = 0;

    // Register holding the final filtered indices.
    interpreter::ReadHandle<Span<uint32_t>> output_register;

    // The maximum number of rows it's possible for this query plan to return.
    uint32_t max_row_count = 0;

    // The number of rows this query plan estimates it will return.
    uint32_t estimated_row_count = 0;

    // The number of registers used by this query plan.
    uint32_t register_count = 0;

    // Number of filter values used by this query.
    uint32_t filter_value_count = 0;

    // Number of output indices per row.
    uint32_t output_per_row = 0;
  };
  static_assert(std::is_trivially_copyable_v<ExecutionParams>);
  static_assert(std::is_trivially_destructible_v<ExecutionParams>);
  static_assert(sizeof(ExecutionParams) == 32);

  // Serializes the query plan to a Base64-encoded string.
  // This allows plans to be stored or transmitted between processes.
  std::string Serialize() const {
    size_t size =
        sizeof(params) + sizeof(size_t) +
        (bytecode.size() * sizeof(interpreter::Bytecode)) + sizeof(size_t) +
        (col_to_output_offset.size() * sizeof(uint32_t)) + sizeof(size_t) +
        (register_inits.size() * sizeof(RegisterInit));
    std::string res(size, '\0');
    char* p = res.data();
    {
      memcpy(p, &params, sizeof(params));
      p += sizeof(params);
    }
    {
      size_t bytecode_size = bytecode.size();
      memcpy(p, &bytecode_size, sizeof(bytecode_size));
      p += sizeof(bytecode_size);
    }
    {
      memcpy(p, bytecode.data(),
             bytecode.size() * sizeof(interpreter::Bytecode));
      p += bytecode.size() * sizeof(interpreter::Bytecode);
    }
    {
      size_t columns_size = col_to_output_offset.size();
      memcpy(p, &columns_size, sizeof(columns_size));
      p += sizeof(columns_size);
    }
    {
      size_t columns_size = col_to_output_offset.size();
      memcpy(p, col_to_output_offset.data(), columns_size * sizeof(uint32_t));
      p += columns_size * sizeof(uint32_t);
    }
    {
      size_t register_inits_size = register_inits.size();
      memcpy(p, &register_inits_size, sizeof(register_inits_size));
      p += sizeof(register_inits_size);
    }
    {
      memcpy(p, register_inits.data(),
             register_inits.size() * sizeof(RegisterInit));
      p += register_inits.size() * sizeof(RegisterInit);
    }
    PERFETTO_CHECK(p == res.data() + res.size());
    return base::Base64Encode(base::StringView(res));
  }

  // Deserializes a query plan from a Base64-encoded string.
  // Returns the reconstructed QueryPlan.
  static QueryPlanImpl Deserialize(std::string_view serialized) {
    QueryPlanImpl res;
    std::optional<std::string> raw_data = base::Base64Decode(
        base::StringView(serialized.data(), serialized.size()));
    PERFETTO_CHECK(raw_data);
    const char* p = raw_data->data();
    size_t bytecode_size;
    size_t columns_size;
    size_t register_inits_size;
    {
      memcpy(&res.params, p, sizeof(res.params));
      p += sizeof(res.params);
    }
    {
      memcpy(&bytecode_size, p, sizeof(bytecode_size));
      p += sizeof(bytecode_size);
    }
    {
      for (uint32_t i = 0; i < bytecode_size; ++i) {
        res.bytecode.emplace_back();
      }
      memcpy(res.bytecode.data(), p,
             bytecode_size * sizeof(interpreter::Bytecode));
      p += bytecode_size * sizeof(interpreter::Bytecode);
    }
    {
      memcpy(&columns_size, p, sizeof(columns_size));
      p += sizeof(columns_size);
    }
    {
      for (uint32_t i = 0; i < columns_size; ++i) {
        res.col_to_output_offset.emplace_back();
      }
      memcpy(res.col_to_output_offset.data(), p,
             columns_size * sizeof(uint32_t));
      p += columns_size * sizeof(uint32_t);
    }
    {
      memcpy(&register_inits_size, p, sizeof(register_inits_size));
      p += sizeof(register_inits_size);
    }
    {
      for (size_t i = 0; i < register_inits_size; ++i) {
        res.register_inits.emplace_back();
      }
      memcpy(res.register_inits.data(), p,
             register_inits_size * sizeof(RegisterInit));
      p += register_inits_size * sizeof(RegisterInit);
    }
    PERFETTO_CHECK(p == raw_data->data() + raw_data->size());
    return res;
  }

  // Converts a RegisterInit spec to the actual register value for execution.
  // Used by Cursor and tree operators to initialize registers before
  // bytecode execution.
  static interpreter::RegValue GetRegisterInitValue(
      const RegisterInit& init,
      const Column* const* columns,
      const Index* indexes);

  ExecutionParams params;
  interpreter::BytecodeVector bytecode;
  base::SmallVector<uint32_t, 24> col_to_output_offset;

  // Register initialization specifications.
  // The cursor processes these to set up registers before bytecode execution.
  base::SmallVector<RegisterInit, 16> register_inits;
};

}  // namespace perfetto::trace_processor::core::dataframe

#endif  // SRC_TRACE_PROCESSOR_CORE_DATAFRAME_QUERY_PLAN_H_
