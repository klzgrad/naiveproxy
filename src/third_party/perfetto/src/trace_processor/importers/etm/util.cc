
/*
 * Copyright (C) 2024 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License";
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

#include "src/trace_processor/importers/etm/util.h"

#include <optional>

#include "perfetto/base/logging.h"
#include "src/trace_processor/importers/etm/element_cursor.h"

namespace perfetto::trace_processor::etm {

const char* ToString(ocsd_gen_trc_elem_t type) {
  // This switch also makes sure the range of values for type is compatible with
  // `ElementTypeMask`.
  switch (type) {
    case OCSD_GEN_TRC_ELEM_UNKNOWN:
      static_assert(
          ElementTypeMask::IsCompatibleValue(OCSD_GEN_TRC_ELEM_UNKNOWN));
      return "UNKNOWN";
    case OCSD_GEN_TRC_ELEM_NO_SYNC:
      static_assert(
          ElementTypeMask::IsCompatibleValue(OCSD_GEN_TRC_ELEM_NO_SYNC));
      return "NO_SYNC";
    case OCSD_GEN_TRC_ELEM_TRACE_ON:
      static_assert(
          ElementTypeMask::IsCompatibleValue(OCSD_GEN_TRC_ELEM_TRACE_ON));
      return "TRACE_ON";
    case OCSD_GEN_TRC_ELEM_EO_TRACE:
      static_assert(
          ElementTypeMask::IsCompatibleValue(OCSD_GEN_TRC_ELEM_EO_TRACE));
      return "EO_TRACE";
    case OCSD_GEN_TRC_ELEM_PE_CONTEXT:
      static_assert(
          ElementTypeMask::IsCompatibleValue(OCSD_GEN_TRC_ELEM_PE_CONTEXT));
      return "PE_CONTEXT";
    case OCSD_GEN_TRC_ELEM_INSTR_RANGE:
      static_assert(
          ElementTypeMask::IsCompatibleValue(OCSD_GEN_TRC_ELEM_INSTR_RANGE));
      return "INSTR_RANGE";
    case OCSD_GEN_TRC_ELEM_I_RANGE_NOPATH:
      static_assert(
          ElementTypeMask::IsCompatibleValue(OCSD_GEN_TRC_ELEM_I_RANGE_NOPATH));
      return "I_RANGE_NOPATH";
    case OCSD_GEN_TRC_ELEM_ADDR_NACC:
      static_assert(
          ElementTypeMask::IsCompatibleValue(OCSD_GEN_TRC_ELEM_ADDR_NACC));
      return "ADDR_NACC";
    case OCSD_GEN_TRC_ELEM_ADDR_UNKNOWN:
      static_assert(
          ElementTypeMask::IsCompatibleValue(OCSD_GEN_TRC_ELEM_ADDR_UNKNOWN));
      return "ADDR_UNKNOWN";
    case OCSD_GEN_TRC_ELEM_EXCEPTION:
      static_assert(
          ElementTypeMask::IsCompatibleValue(OCSD_GEN_TRC_ELEM_EXCEPTION));
      return "EXCEPTION";
    case OCSD_GEN_TRC_ELEM_EXCEPTION_RET:
      static_assert(
          ElementTypeMask::IsCompatibleValue(OCSD_GEN_TRC_ELEM_EXCEPTION_RET));
      return "EXCEPTION_RET";
    case OCSD_GEN_TRC_ELEM_TIMESTAMP:
      static_assert(
          ElementTypeMask::IsCompatibleValue(OCSD_GEN_TRC_ELEM_TIMESTAMP));
      return "TIMESTAMP";
    case OCSD_GEN_TRC_ELEM_CYCLE_COUNT:
      static_assert(
          ElementTypeMask::IsCompatibleValue(OCSD_GEN_TRC_ELEM_CYCLE_COUNT));
      return "CYCLE_COUNT";
    case OCSD_GEN_TRC_ELEM_EVENT:
      static_assert(
          ElementTypeMask::IsCompatibleValue(OCSD_GEN_TRC_ELEM_EVENT));
      return "EVENT";
    case OCSD_GEN_TRC_ELEM_SWTRACE:
      static_assert(
          ElementTypeMask::IsCompatibleValue(OCSD_GEN_TRC_ELEM_SWTRACE));
      return "SWTRACE";
    case OCSD_GEN_TRC_ELEM_SYNC_MARKER:
      static_assert(
          ElementTypeMask::IsCompatibleValue(OCSD_GEN_TRC_ELEM_SYNC_MARKER));
      return "SYNC_MARKER";
    case OCSD_GEN_TRC_ELEM_MEMTRANS:
      static_assert(
          ElementTypeMask::IsCompatibleValue(OCSD_GEN_TRC_ELEM_MEMTRANS));
      return "MEMTRANS";
    case OCSD_GEN_TRC_ELEM_INSTRUMENTATION:
      static_assert(ElementTypeMask::IsCompatibleValue(
          OCSD_GEN_TRC_ELEM_INSTRUMENTATION));
      return "INSTRUMENTATION";
    case OCSD_GEN_TRC_ELEM_CUSTOM:
      static_assert(
          ElementTypeMask::IsCompatibleValue(OCSD_GEN_TRC_ELEM_CUSTOM));
      return "CUSTOM";
  }
  PERFETTO_CHECK(false);  // For GCC.
}

std::optional<ocsd_gen_trc_elem_t> FromString(const char* type_str) {
  if (strcmp(type_str, "UNKNOWN") == 0) {
    return OCSD_GEN_TRC_ELEM_UNKNOWN;
  }
  if (strcmp(type_str, "NO_SYNC") == 0) {
    return OCSD_GEN_TRC_ELEM_NO_SYNC;
  }
  if (strcmp(type_str, "TRACE_ON") == 0) {
    return OCSD_GEN_TRC_ELEM_TRACE_ON;
  }
  if (strcmp(type_str, "EO_TRACE") == 0) {
    return OCSD_GEN_TRC_ELEM_EO_TRACE;
  }
  if (strcmp(type_str, "PE_CONTEXT") == 0) {
    return OCSD_GEN_TRC_ELEM_PE_CONTEXT;
  }
  if (strcmp(type_str, "INSTR_RANGE") == 0) {
    return OCSD_GEN_TRC_ELEM_INSTR_RANGE;
  }
  if (strcmp(type_str, "I_RANGE_NOPATH") == 0) {
    return OCSD_GEN_TRC_ELEM_I_RANGE_NOPATH;
  }
  if (strcmp(type_str, "ADDR_NACC") == 0) {
    return OCSD_GEN_TRC_ELEM_ADDR_NACC;
  }
  if (strcmp(type_str, "ADDR_UNKNOWN") == 0) {
    return OCSD_GEN_TRC_ELEM_ADDR_UNKNOWN;
  }
  if (strcmp(type_str, "EXCEPTION") == 0) {
    return OCSD_GEN_TRC_ELEM_EXCEPTION;
  }
  if (strcmp(type_str, "EXCEPTION_RET") == 0) {
    return OCSD_GEN_TRC_ELEM_EXCEPTION_RET;
  }
  if (strcmp(type_str, "TIMESTAMP") == 0) {
    return OCSD_GEN_TRC_ELEM_TIMESTAMP;
  }
  if (strcmp(type_str, "CYCLE_COUNT") == 0) {
    return OCSD_GEN_TRC_ELEM_CYCLE_COUNT;
  }
  if (strcmp(type_str, "EVENT") == 0) {
    return OCSD_GEN_TRC_ELEM_EVENT;
  }
  if (strcmp(type_str, "SWTRACE") == 0) {
    return OCSD_GEN_TRC_ELEM_SWTRACE;
  }
  if (strcmp(type_str, "SYNC_MARKER") == 0) {
    return OCSD_GEN_TRC_ELEM_SYNC_MARKER;
  }
  if (strcmp(type_str, "MEMTRANS") == 0) {
    return OCSD_GEN_TRC_ELEM_MEMTRANS;
  }
  if (strcmp(type_str, "INSTRUMENTATION") == 0) {
    return OCSD_GEN_TRC_ELEM_INSTRUMENTATION;
  }
  if (strcmp(type_str, "CUSTOM") == 0) {
    return OCSD_GEN_TRC_ELEM_CUSTOM;
  }
  return std::nullopt;
}

const char* ToString(ocsd_isa isa) {
  switch (isa) {
    case ocsd_isa_arm:
      return "ARM";
    case ocsd_isa_thumb2:
      return "THUMB2";
    case ocsd_isa_aarch64:
      return "AARCH64";
    case ocsd_isa_tee:
      return "TEE";
    case ocsd_isa_jazelle:
      return "JAZELLE";
    case ocsd_isa_custom:
      return "CUSTOM";
    case ocsd_isa_unknown:
      return "UNKNOWN";
  }
  PERFETTO_CHECK(false);  // For GCC.
}

const char* ToString(ocsd_instr_type type) {
  switch (type) {
    case OCSD_INSTR_OTHER:
      return "OTHER";
    case OCSD_INSTR_BR:
      return "BR";
    case OCSD_INSTR_BR_INDIRECT:
      return "BR_INDIRECT";
    case OCSD_INSTR_ISB:
      return "ISB";
    case OCSD_INSTR_DSB_DMB:
      return "DSB_DMB";
    case OCSD_INSTR_WFI_WFE:
      return "WFI_WFE";
    case OCSD_INSTR_TSTART:
      return "TSTART";
  }
  PERFETTO_CHECK(false);  // For GCC.
}

const char* ToString(ocsd_instr_subtype sub_type) {
  switch (sub_type) {
    case OCSD_S_INSTR_NONE:
      return "NONE";
    case OCSD_S_INSTR_BR_LINK:
      return "BR_LINK";
    case OCSD_S_INSTR_V8_RET:
      return "V8_RET";
    case OCSD_S_INSTR_V8_ERET:
      return "V8_ERET";
    case OCSD_S_INSTR_V7_IMPLIED_RET:
      return "V7_IMPLIED_RET";
  }
  PERFETTO_CHECK(false);  // For GCC.
}

const char* ToString(ocsd_core_profile_t profile) {
  switch (profile) {
    case profile_Unknown:
      return "UNKNOWN";
    case profile_CortexM:
      return "CORTEX_M";
    case profile_CortexR:
      return "CORTEX_R";
    case profile_CortexA:
      return "CORTEX_A";
    case profile_Custom:
      return "CUSTOM";
  }
  return "UNKNOWN";
}

const char* ToString(ocsd_arch_version_t ver) {
  switch (ver) {
    case ARCH_UNKNOWN:
      return "UNKNOWN";
    case ARCH_CUSTOM:
      return "CUSTOM";
    case ARCH_V7:
      return "V7";
    case ARCH_V8:
      return "V8";
    case ARCH_V8r3:
      return "V8_R3";
    case ARCH_AA64:
      return "AA64";
  }
  return "UNKNOWN";
}

}  // namespace perfetto::trace_processor::etm
