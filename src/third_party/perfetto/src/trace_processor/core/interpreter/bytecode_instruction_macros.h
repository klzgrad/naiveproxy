/*
 * Copyright (C) 2026 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_CORE_INTERPRETER_BYTECODE_INSTRUCTION_MACROS_H_
#define SRC_TRACE_PROCESSOR_CORE_INTERPRETER_BYTECODE_INSTRUCTION_MACROS_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <utility>

#include "perfetto/base/logging.h"
#include "perfetto/public/compiler.h"
#include "src/trace_processor/core/interpreter/bytecode_core.h"
#include "src/trace_processor/core/interpreter/bytecode_to_string.h"

namespace perfetto::trace_processor::core::interpreter {

// Maximum number of arguments in any bytecode instruction.
inline constexpr size_t kMaxBytecodeArgs = 9;

template <typename T, size_t... I>
constexpr auto MakeOffsetsArrayImpl(std::index_sequence<I...>) {
  // Fixed size array for consistent sizing across all bytecodes.
  std::array<uint32_t, kMaxBytecodeArgs + 1> offsets{};
  ((offsets[I + 1] = offsets[I] + sizeof(std::tuple_element_t<I, T>)), ...);
  // Ensure that all the types have aligned offsets.
  ((offsets[I + 1] % alignof(std::tuple_element_t<I, T>) == 0
        ? void()
        : PERFETTO_ELOG("Type index %zu is not aligned to %zu", I,
                        alignof(std::tuple_element_t<I, T>))),
   ...);
  return offsets;
}

// Helper for generating the offsets array for instruction arguments.
// Returns a fixed-size array (kMaxBytecodeArgs + 1) for uniform sizing.
template <typename T>
constexpr std::array<uint32_t, kMaxBytecodeArgs + 1> MakeOffsetsArray() {
  constexpr auto kOffsets =
      MakeOffsetsArrayImpl<T>(std::make_index_sequence<std::tuple_size_v<T>>());
  static_assert(kOffsets[std::tuple_size_v<T>] <=
                sizeof(Bytecode::args_buffer));
  return kOffsets;
}

// Bytecode with one template parameter for dispatching.
template <typename TypeSet1>
struct TemplatedBytecode1 : Bytecode {
  using TS1 = TypeSet1;
  PERFETTO_ALWAYS_INLINE static constexpr uint32_t OpcodeOffset(const TS1& ts) {
    return ts.index();
  }
};

// Bytecode with two template parameters for dispatching.
template <typename TypeSet1, typename TypeSet2>
struct TemplatedBytecode2 : Bytecode {
  using TS1 = TypeSet1;
  using TS2 = TypeSet2;
  PERFETTO_ALWAYS_INLINE static constexpr uint32_t OpcodeOffset(
      const TS1& ts1,
      const TS2& ts2) {
    return (ts1.index() * TS2::kSize) + ts2.index();
  }
};

// Macro to define bytecode instruction with 5 fields.
#define PERFETTO_DATAFRAME_BYTECODE_IMPL_9(t1, n1, t2, n2, t3, n3, t4, n4, t5, \
                                           n5, t6, n6, t7, n7, t8, n8, t9, n9) \
  enum Field : uint8_t { n1 = 0, n2, n3, n4, n5, n6, n7, n8, n9 };             \
  using tuple = std::tuple<t1, t2, t3, t4, t5, t6, t7, t8, t9>;                \
  static constexpr auto kOffsets = MakeOffsetsArray<tuple>();                  \
  static constexpr auto kNames =                                               \
      std::array{#n1, #n2, #n3, #n4, #n5, #n6, #n7, #n8, #n9};                 \
                                                                               \
  template <Field N>                                                           \
  const auto& arg() const {                                                    \
    return *reinterpret_cast<const std::tuple_element_t<N, tuple>*>(           \
        args_buffer.data() + kOffsets[N]);                                     \
  }                                                                            \
  template <Field N>                                                           \
  auto& arg() {                                                                \
    return *reinterpret_cast<std::tuple_element_t<N, tuple>*>(                 \
        args_buffer.data() + kOffsets[N]);                                     \
  }                                                                            \
  std::string ToString() const {                                               \
    std::vector<std::string> fields;                                           \
    BytecodeFieldToString(#n1, ArgToString(arg<n1>()).c_str(), fields);        \
    BytecodeFieldToString(#n2, ArgToString(arg<n2>()).c_str(), fields);        \
    BytecodeFieldToString(#n3, ArgToString(arg<n3>()).c_str(), fields);        \
    BytecodeFieldToString(#n4, ArgToString(arg<n4>()).c_str(), fields);        \
    BytecodeFieldToString(#n5, ArgToString(arg<n5>()).c_str(), fields);        \
    BytecodeFieldToString(#n6, ArgToString(arg<n6>()).c_str(), fields);        \
    BytecodeFieldToString(#n7, ArgToString(arg<n7>()).c_str(), fields);        \
    BytecodeFieldToString(#n8, ArgToString(arg<n8>()).c_str(), fields);        \
    BytecodeFieldToString(#n9, ArgToString(arg<n9>()).c_str(), fields);        \
    return BytecodeFieldsFormat(fields);                                       \
  }                                                                            \
  static void UnusedForWarningSuppresssion()

// Simplified macros that add padding fields automatically.
#define PERFETTO_DATAFRAME_BYTECODE_IMPL_8(t1, n1, t2, n2, t3, n3, t4, n4, t5, \
                                           n5, t6, n6, t7, n7, t8, n8)         \
  PERFETTO_DATAFRAME_BYTECODE_IMPL_9(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5,   \
                                     t6, n6, t7, n7, t8, n8, uint32_t, pad9)

// Simplified macros that add padding fields automatically.
#define PERFETTO_DATAFRAME_BYTECODE_IMPL_7(t1, n1, t2, n2, t3, n3, t4, n4, t5, \
                                           n5, t6, n6, t7, n7)                 \
  PERFETTO_DATAFRAME_BYTECODE_IMPL_8(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5,   \
                                     t6, n6, t7, n7, uint32_t, pad8)

#define PERFETTO_DATAFRAME_BYTECODE_IMPL_6(t1, n1, t2, n2, t3, n3, t4, n4, t5, \
                                           n5, t6, n6)                         \
  PERFETTO_DATAFRAME_BYTECODE_IMPL_7(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5,   \
                                     t6, n6, uint32_t, pad7)

#define PERFETTO_DATAFRAME_BYTECODE_IMPL_5(t1, n1, t2, n2, t3, n3, t4, n4, t5, \
                                           n5)                                 \
  PERFETTO_DATAFRAME_BYTECODE_IMPL_6(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5,   \
                                     uint32_t, pad6)

#define PERFETTO_DATAFRAME_BYTECODE_IMPL_4(t1, n1, t2, n2, t3, n3, t4, n4)     \
  PERFETTO_DATAFRAME_BYTECODE_IMPL_5(t1, n1, t2, n2, t3, n3, t4, n4, uint32_t, \
                                     pad5)

#define PERFETTO_DATAFRAME_BYTECODE_IMPL_3(t1, n1, t2, n2, t3, n3) \
  PERFETTO_DATAFRAME_BYTECODE_IMPL_4(t1, n1, t2, n2, t3, n3, uint32_t, pad4)

#define PERFETTO_DATAFRAME_BYTECODE_IMPL_2(t1, n1, t2, n2) \
  PERFETTO_DATAFRAME_BYTECODE_IMPL_3(t1, n1, t2, n2, uint32_t, pad3)

#define PERFETTO_DATAFRAME_BYTECODE_IMPL_1(t1, n1) \
  PERFETTO_DATAFRAME_BYTECODE_IMPL_2(t1, n1, uint32_t, pad2)

}  // namespace perfetto::trace_processor::core::interpreter

#endif  // SRC_TRACE_PROCESSOR_CORE_INTERPRETER_BYTECODE_INSTRUCTION_MACROS_H_
