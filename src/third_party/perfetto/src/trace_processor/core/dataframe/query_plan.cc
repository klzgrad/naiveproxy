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

#include "src/trace_processor/core/dataframe/query_plan.h"

#include "perfetto/base/logging.h"
#include "src/trace_processor/core/dataframe/dataframe.h"
#include "src/trace_processor/core/dataframe/types.h"
#include "src/trace_processor/core/interpreter/interpreter_types.h"
#include "src/trace_processor/core/util/span.h"

namespace perfetto::trace_processor::core::dataframe {
namespace {
namespace i = interpreter;
}  // namespace

i::RegValue QueryPlanImpl::GetRegisterInitValue(const RegisterInit& init,
                                                const Column* const* columns,
                                                const Index* indexes) {
  switch (init.kind.index()) {
    case RegisterInit::Type::GetTypeIndex<Id>():
      // Id columns don't have actual storage - the row index IS the value.
      // Return a nullptr StoragePtr which the interpreter knows to handle.
      return i::StoragePtr{nullptr, Id{}};
    case RegisterInit::Type::GetTypeIndex<Uint32>():
      return i::StoragePtr{
          columns[init.source_index]->storage.unchecked_data<Uint32>(),
          Uint32{},
      };
    case RegisterInit::Type::GetTypeIndex<Int32>():
      return i::StoragePtr{
          columns[init.source_index]->storage.unchecked_data<Int32>(),
          Int32{},
      };
    case RegisterInit::Type::GetTypeIndex<Int64>():
      return i::StoragePtr{
          columns[init.source_index]->storage.unchecked_data<Int64>(),
          Int64{},
      };
    case RegisterInit::Type::GetTypeIndex<Double>():
      return i::StoragePtr{
          columns[init.source_index]->storage.unchecked_data<Double>(),
          Double{},
      };
    case RegisterInit::Type::GetTypeIndex<String>():
      return i::StoragePtr{
          columns[init.source_index]->storage.unchecked_data<String>(),
          String{},
      };
    case RegisterInit::Type::GetTypeIndex<RegisterInit::NullBitvector>(): {
      i::NullBitvector nbv;
      nbv.bv = columns[init.source_index]->null_storage.MaybeGetNullBitVector();
      return nbv;
    }
    case RegisterInit::Type::GetTypeIndex<RegisterInit::IndexVector>():
      return Span<uint32_t>(
          indexes[init.source_index].permutation_vector()->data(),
          indexes[init.source_index].permutation_vector()->data() +
              indexes[init.source_index].permutation_vector()->size());
    case RegisterInit::Type::GetTypeIndex<
        RegisterInit::SmallValueEqBitvector>(): {
      const auto& sve = columns[init.source_index]
                            ->specialized_storage
                            .unchecked_get<SpecializedStorage::SmallValueEq>();
      return &sve.bit_vector;
    }
    case RegisterInit::Type::GetTypeIndex<
        RegisterInit::SmallValueEqPopcount>(): {
      const auto& sve = columns[init.source_index]
                            ->specialized_storage
                            .unchecked_get<SpecializedStorage::SmallValueEq>();
      return Span<const uint32_t>(
          sve.prefix_popcount.data(),
          sve.prefix_popcount.data() + sve.prefix_popcount.size());
    }
    default:
      PERFETTO_FATAL("Unhandled RegisterInit kind: %u",
                     static_cast<uint32_t>(init.kind.index()));
  }
}

}  // namespace perfetto::trace_processor::core::dataframe
