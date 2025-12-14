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

#include "src/trace_processor/perfetto_sql/intrinsics/functions/to_ftrace.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <optional>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/fixed_string_writer.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/public/compiler.h"
#include "src/trace_processor/containers/null_term_string_view.h"
#include "src/trace_processor/dataframe/specs.h"
#include "src/trace_processor/importers/common/system_info_tracker.h"
#include "src/trace_processor/importers/ftrace/ftrace_descriptors.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/sqlite/bindings/sqlite_type.h"
#include "src/trace_processor/sqlite/bindings/sqlite_value.h"
#include "src/trace_processor/sqlite/sqlite_utils.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/metadata_tables_py.h"
#include "src/trace_processor/types/gfp_flags.h"
#include "src/trace_processor/types/softirq_action.h"
#include "src/trace_processor/types/task_state.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/types/variadic.h"

#include "protos/perfetto/trace/ftrace/binder.pbzero.h"
#include "protos/perfetto/trace/ftrace/cgroup.pbzero.h"
#include "protos/perfetto/trace/ftrace/clk.pbzero.h"
#include "protos/perfetto/trace/ftrace/dpu.pbzero.h"
#include "protos/perfetto/trace/ftrace/filemap.pbzero.h"
#include "protos/perfetto/trace/ftrace/ftrace.pbzero.h"
#include "protos/perfetto/trace/ftrace/ftrace_event.pbzero.h"
#include "protos/perfetto/trace/ftrace/g2d.pbzero.h"
#include "protos/perfetto/trace/ftrace/irq.pbzero.h"
#include "protos/perfetto/trace/ftrace/mdss.pbzero.h"
#include "protos/perfetto/trace/ftrace/panel.pbzero.h"
#include "protos/perfetto/trace/ftrace/power.pbzero.h"
#include "protos/perfetto/trace/ftrace/samsung.pbzero.h"
#include "protos/perfetto/trace/ftrace/sched.pbzero.h"
#include "protos/perfetto/trace/ftrace/timer.pbzero.h"
#include "protos/perfetto/trace/ftrace/workqueue.pbzero.h"
#include "src/trace_processor/types/version_number.h"

namespace perfetto::trace_processor {

namespace {

struct FtraceTime {
  FtraceTime(int64_t ns)
      : secs(ns / 1000000000LL), micros((ns - secs * 1000000000LL) / 1000) {}

  const int64_t secs;
  const int64_t micros;
};

class ArgsSerializer {
 public:
  ArgsSerializer(TraceProcessorContext*,
                 ArgSetId arg_set_id,
                 tables::ArgTable::ConstCursor*,
                 NullTermStringView event_name,
                 std::vector<std::optional<uint32_t>>* field_id_to_arg_index,
                 base::FixedStringWriter*);

  void SerializeArgs();

 private:
  using ValueWriter = std::function<void(const Variadic&)>;
  using SerializerValueWriter = void (ArgsSerializer::*)(const Variadic&);

  // Arg writing functions.
  void WriteArgForField(uint32_t field_id, const ValueWriter& writer) {
    std::optional<uint32_t> row = FieldIdToRow(field_id);
    if (!row)
      return;
    WriteArgAtRow(*row, writer);
  }
  void WriteArgForField(uint32_t field_id,
                        base::StringView key,
                        const ValueWriter& writer) {
    std::optional<uint32_t> row = FieldIdToRow(field_id);
    if (!row)
      return;
    WriteArg(key, storage_->GetArgValue(*row), writer);
  }
  void WriteArgAtRow(uint32_t arg_row, const ValueWriter& writer) {
    const auto& args = storage_->arg_table();
    const auto& key = storage_->GetString(args[arg_row].key());
    WriteArg(key, storage_->GetArgValue(arg_row), writer);
  }
  void WriteArg(base::StringView key,
                Variadic value,
                const ValueWriter& writer);

  // Value writing functions.
  void WriteValueForField(uint32_t field_id, const ValueWriter& writer) {
    std::optional<uint32_t> row = FieldIdToRow(field_id);
    if (!row)
      return;
    writer(storage_->GetArgValue(*row));
  }
  void WriteKernelFnValue(const Variadic& value) {
    if (value.type == Variadic::Type::kUint) {
      writer_->AppendHexInt(value.uint_value);
    } else if (value.type == Variadic::Type::kString) {
      WriteValue(value);
    } else {
      PERFETTO_DFATAL("Invalid field type %d", static_cast<int>(value.type));
    }
  }
  void WriteValue(const Variadic&);

  // The default value writer which uses the |WriteValue| function.
  ValueWriter DVW() { return Wrap(&ArgsSerializer::WriteValue); }
  ValueWriter Wrap(SerializerValueWriter writer) {
    return [this, writer](const Variadic& v) { (this->*writer)(v); };
  }

  // Converts a field id to a row in the args table.
  std::optional<uint32_t> FieldIdToRow(uint32_t field_id) {
    PERFETTO_DCHECK(field_id > 0);
    PERFETTO_DCHECK(field_id < field_id_to_arg_index_->size());
    std::optional<uint32_t> index_in_arg_set =
        (*field_id_to_arg_index_)[field_id];
    return index_in_arg_set.has_value()
               ? std::make_optional(start_row_ + *index_in_arg_set)
               : std::nullopt;
  }

  const TraceStorage* storage_ = nullptr;
  TraceProcessorContext* context_ = nullptr;
  tables::ArgTable::ConstCursor* cursor_;
  NullTermStringView event_name_;
  std::vector<std::optional<uint32_t>>* field_id_to_arg_index_;

  uint32_t start_row_ = 0;

  base::FixedStringWriter* writer_ = nullptr;
};

ArgsSerializer::ArgsSerializer(
    TraceProcessorContext* context,
    ArgSetId arg_set_id,
    tables::ArgTable::ConstCursor* cursor,
    NullTermStringView event_name,
    std::vector<std::optional<uint32_t>>* field_id_to_arg_index,
    base::FixedStringWriter* writer)
    : storage_(context->storage.get()),
      context_(context),
      cursor_(cursor),
      event_name_(event_name),
      field_id_to_arg_index_(field_id_to_arg_index),
      writer_(writer) {
  cursor_->SetFilterValueUnchecked(0, arg_set_id);
  cursor_->Execute();

  // We assume that the row map is a contiguous range (which is always the case
  // because arg_set_ids are contiguous by definition).
  start_row_ = cursor_->Eof() ? 0 : cursor_->ToRowNumber().row_number();

  // If the vector already has entries, we've previously cached the mapping
  // from field id to arg index.
  if (!field_id_to_arg_index->empty())
    return;

  auto* descriptor = GetMessageDescriptorForName(event_name);
  if (!descriptor) {
    // If we don't have a descriptor, this event must be a generic ftrace event.
    // As we can't possibly have any special handling for generic events, just
    // add a row to the vector (for the invalid field id 0) to remove future
    // lookups for this event name.
    field_id_to_arg_index->resize(1);
    return;
  }

  // If we have a descriptor, try and create the mapping from proto field id
  // to the index in the arg set.
  size_t max = descriptor->max_field_id;

  // We need to reserve an index for the invalid field id 0.
  field_id_to_arg_index_->resize(max + 1);

  // Go through each field id and find the entry in the args table for that
  for (uint32_t r = 0; !cursor_->Eof(); cursor_->Next(), ++r) {
    for (uint32_t i = 1; i <= max; ++i) {
      base::StringView key = context->storage->GetString(cursor_->key());
      if (key == descriptor->fields[i].name) {
        (*field_id_to_arg_index)[i] = r;
        break;
      }
    }
  }

  // Reset the cursor to the start row so that we can serialize the args
  // correctly.
  cursor_->SetFilterValueUnchecked(0, arg_set_id);
  cursor_->Execute();
}

void ArgsSerializer::SerializeArgs() {
  if (cursor_->Eof())
    return;

  if (event_name_ == "sched_switch") {
    using SS = protos::pbzero::SchedSwitchFtraceEvent;

    WriteArgForField(SS::kPrevCommFieldNumber, DVW());
    WriteArgForField(SS::kPrevPidFieldNumber, DVW());
    WriteArgForField(SS::kPrevPrioFieldNumber, DVW());
    WriteArgForField(SS::kPrevStateFieldNumber, [this](const Variadic& value) {
      PERFETTO_DCHECK(value.type == Variadic::Type::kInt);
      auto state = static_cast<uint16_t>(value.int_value);
      std::optional<VersionNumber> kernel_version =
          SystemInfoTracker::GetOrCreate(context_)->GetKernelVersion();
      writer_->AppendString(
          ftrace_utils::TaskState::FromRawPrevState(state, kernel_version)
              .ToString('|')
              .data());
    });
    writer_->AppendLiteral(" ==>");
    WriteArgForField(SS::kNextCommFieldNumber, DVW());
    WriteArgForField(SS::kNextPidFieldNumber, DVW());
    WriteArgForField(SS::kNextPrioFieldNumber, DVW());
    return;
  } else if (event_name_ == "sched_wakeup") {
    using SW = protos::pbzero::SchedWakeupFtraceEvent;
    WriteArgForField(SW::kCommFieldNumber, DVW());
    WriteArgForField(SW::kPidFieldNumber, DVW());
    WriteArgForField(SW::kPrioFieldNumber, DVW());
    WriteArgForField(SW::kTargetCpuFieldNumber, [this](const Variadic& value) {
      PERFETTO_DCHECK(value.type == Variadic::Type::kInt);
      writer_->AppendPaddedInt<'0', 3>(value.int_value);
    });
    return;
  } else if (event_name_ == "clock_set_rate") {
    using CSR = protos::pbzero::ClockSetRateFtraceEvent;
    writer_->AppendLiteral(" ");
    WriteValueForField(CSR::kNameFieldNumber, DVW());
    WriteArgForField(CSR::kStateFieldNumber, DVW());
    WriteArgForField(CSR::kCpuIdFieldNumber, DVW());
    return;
  } else if (event_name_ == "clk_set_rate") {
    using CSR = protos::pbzero::ClkSetRateFtraceEvent;
    writer_->AppendLiteral(" ");
    WriteValueForField(CSR::kNameFieldNumber, DVW());
    writer_->AppendLiteral(" ");
    WriteValueForField(CSR::kRateFieldNumber, DVW());
    return;
  } else if (event_name_ == "clock_enable") {
    using CE = protos::pbzero::ClockEnableFtraceEvent;
    WriteValueForField(CE::kNameFieldNumber, DVW());
    WriteArgForField(CE::kStateFieldNumber, DVW());
    WriteArgForField(CE::kCpuIdFieldNumber, DVW());
    return;
  } else if (event_name_ == "clock_disable") {
    using CD = protos::pbzero::ClockDisableFtraceEvent;
    WriteValueForField(CD::kNameFieldNumber, DVW());
    WriteArgForField(CD::kStateFieldNumber, DVW());
    WriteArgForField(CD::kCpuIdFieldNumber, DVW());
    return;
  } else if (event_name_ == "binder_transaction") {
    using BT = protos::pbzero::BinderTransactionFtraceEvent;
    writer_->AppendString(" transaction=");
    WriteValueForField(BT::kDebugIdFieldNumber, [this](const Variadic& value) {
      PERFETTO_DCHECK(value.type == Variadic::Type::kInt);
      writer_->AppendUnsignedInt(static_cast<uint32_t>(value.int_value));
    });

    writer_->AppendString(" dest_node=");
    WriteValueForField(
        BT::kTargetNodeFieldNumber, [this](const Variadic& value) {
          PERFETTO_DCHECK(value.type == Variadic::Type::kInt);
          writer_->AppendUnsignedInt(static_cast<uint32_t>(value.int_value));
        });

    writer_->AppendString(" dest_proc=");
    WriteValueForField(BT::kToProcFieldNumber, DVW());

    writer_->AppendString(" dest_thread=");
    WriteValueForField(BT::kToThreadFieldNumber, DVW());

    writer_->AppendString(" reply=");
    WriteValueForField(BT::kReplyFieldNumber, DVW());

    writer_->AppendString(" flags=0x");
    WriteValueForField(BT::kFlagsFieldNumber, [this](const Variadic& value) {
      PERFETTO_DCHECK(value.type == Variadic::Type::kUint);
      writer_->AppendHexInt(value.uint_value);
    });

    writer_->AppendString(" code=0x");
    WriteValueForField(BT::kCodeFieldNumber, [this](const Variadic& value) {
      PERFETTO_DCHECK(value.type == Variadic::Type::kUint);
      writer_->AppendHexInt(value.uint_value);
    });
    return;
  } else if (event_name_ == "binder_transaction_alloc_buf") {
    using BTAB = protos::pbzero::BinderTransactionAllocBufFtraceEvent;
    writer_->AppendString(" transaction=");
    WriteValueForField(
        BTAB::kDebugIdFieldNumber, [this](const Variadic& value) {
          PERFETTO_DCHECK(value.type == Variadic::Type::kInt);
          writer_->AppendUnsignedInt(static_cast<uint32_t>(value.int_value));
        });
    WriteArgForField(BTAB::kDataSizeFieldNumber, DVW());
    WriteArgForField(BTAB::kOffsetsSizeFieldNumber, DVW());
    return;
  } else if (event_name_ == "binder_transaction_received") {
    using BTR = protos::pbzero::BinderTransactionReceivedFtraceEvent;
    writer_->AppendString(" transaction=");
    WriteValueForField(BTR::kDebugIdFieldNumber, [this](const Variadic& value) {
      PERFETTO_DCHECK(value.type == Variadic::Type::kInt);
      writer_->AppendUnsignedInt(static_cast<uint32_t>(value.int_value));
    });
    return;
  } else if (event_name_ == "mm_filemap_add_to_page_cache") {
    using MFA = protos::pbzero::MmFilemapAddToPageCacheFtraceEvent;
    writer_->AppendString(" dev ");
    WriteValueForField(MFA::kSDevFieldNumber, [this](const Variadic& value) {
      PERFETTO_DCHECK(value.type == Variadic::Type::kUint);
      writer_->AppendUnsignedInt(value.uint_value >> 20);
    });
    writer_->AppendString(":");
    WriteValueForField(MFA::kSDevFieldNumber, [this](const Variadic& value) {
      PERFETTO_DCHECK(value.type == Variadic::Type::kUint);
      writer_->AppendUnsignedInt(value.uint_value & ((1 << 20) - 1));
    });
    writer_->AppendString(" ino ");
    WriteValueForField(MFA::kIInoFieldNumber, [this](const Variadic& value) {
      PERFETTO_DCHECK(value.type == Variadic::Type::kUint);
      writer_->AppendHexInt(value.uint_value);
    });
    writer_->AppendString(" page=0000000000000000");
    writer_->AppendString(" pfn=");
    WriteValueForField(MFA::kPfnFieldNumber, DVW());
    writer_->AppendString(" ofs=");
    WriteValueForField(MFA::kIndexFieldNumber, [this](const Variadic& value) {
      PERFETTO_DCHECK(value.type == Variadic::Type::kUint);
      writer_->AppendUnsignedInt(value.uint_value << 12);
    });
    return;
  } else if (event_name_ == "print") {
    using P = protos::pbzero::PrintFtraceEvent;

    writer_->AppendChar(' ');
    WriteValueForField(P::kBufFieldNumber, [this](const Variadic& value) {
      PERFETTO_DCHECK(value.type == Variadic::Type::kString);

      NullTermStringView str = storage_->GetString(value.string_value);
      // If the last character is a newline in a print, just drop it.
      auto chars_to_print = !str.empty() && str.c_str()[str.size() - 1] == '\n'
                                ? str.size() - 1
                                : str.size();
      writer_->AppendString(str.c_str(), chars_to_print);
    });
    return;
  } else if (event_name_ == "sched_blocked_reason") {
    using SBR = protos::pbzero::SchedBlockedReasonFtraceEvent;
    WriteArgForField(SBR::kPidFieldNumber, DVW());
    WriteArgForField(SBR::kIoWaitFieldNumber, "iowait", DVW());
    WriteArgForField(SBR::kCallerFieldNumber,
                     Wrap(&ArgsSerializer::WriteKernelFnValue));
    return;
  } else if (event_name_ == "workqueue_activate_work") {
    using WAW = protos::pbzero::WorkqueueActivateWorkFtraceEvent;
    writer_->AppendString(" work struct ");
    WriteValueForField(WAW::kWorkFieldNumber, [this](const Variadic& value) {
      PERFETTO_DCHECK(value.type == Variadic::Type::kUint);
      writer_->AppendHexInt(value.uint_value);
    });
    return;
  } else if (event_name_ == "workqueue_execute_start") {
    using WES = protos::pbzero::WorkqueueExecuteStartFtraceEvent;
    writer_->AppendString(" work struct ");
    WriteValueForField(WES::kWorkFieldNumber, [this](const Variadic& value) {
      PERFETTO_DCHECK(value.type == Variadic::Type::kUint);
      writer_->AppendHexInt(value.uint_value);
    });
    writer_->AppendString(": function ");
    WriteValueForField(WES::kFunctionFieldNumber,
                       Wrap(&ArgsSerializer::WriteKernelFnValue));
    return;
  } else if (event_name_ == "workqueue_execute_end") {
    using WE = protos::pbzero::WorkqueueExecuteEndFtraceEvent;
    writer_->AppendString(" work struct ");
    WriteValueForField(WE::kWorkFieldNumber, [this](const Variadic& value) {
      PERFETTO_DCHECK(value.type == Variadic::Type::kUint);
      writer_->AppendHexInt(value.uint_value);
    });
    return;
  } else if (event_name_ == "workqueue_queue_work") {
    using WQW = protos::pbzero::WorkqueueQueueWorkFtraceEvent;
    writer_->AppendString(" work struct=");
    WriteValueForField(WQW::kWorkFieldNumber, [this](const Variadic& value) {
      PERFETTO_DCHECK(value.type == Variadic::Type::kUint);
      writer_->AppendHexInt(value.uint_value);
    });
    WriteArgForField(WQW::kFunctionFieldNumber,
                     Wrap(&ArgsSerializer::WriteKernelFnValue));
    WriteArgForField(WQW::kWorkqueueFieldNumber, [this](const Variadic& value) {
      PERFETTO_DCHECK(value.type == Variadic::Type::kUint);
      writer_->AppendHexInt(value.uint_value);
    });
    WriteValueForField(WQW::kReqCpuFieldNumber, DVW());
    WriteValueForField(WQW::kCpuFieldNumber, DVW());
    return;
  } else if (event_name_ == "irq_handler_entry") {
    using IEN = protos::pbzero::IrqHandlerEntryFtraceEvent;
    WriteArgForField(IEN::kIrqFieldNumber, DVW());
    WriteArgForField(IEN::kNameFieldNumber, DVW());
    return;
  } else if (event_name_ == "irq_handler_exit") {
    using IEX = protos::pbzero::IrqHandlerExitFtraceEvent;
    WriteArgForField(IEX::kIrqFieldNumber, DVW());
    writer_->AppendString(" ret=");
    WriteValueForField(IEX::kRetFieldNumber, [this](const Variadic& value) {
      PERFETTO_DCHECK(value.type == Variadic::Type::kInt);
      writer_->AppendString(value.int_value ? "handled" : "unhandled");
    });
    return;
  } else if (event_name_ == "softirq_entry") {
    using SIE = protos::pbzero::SoftirqEntryFtraceEvent;
    WriteArgForField(SIE::kVecFieldNumber, DVW());
    writer_->AppendString(" [action=");
    WriteValueForField(SIE::kVecFieldNumber, [this](const Variadic& value) {
      PERFETTO_DCHECK(value.type == Variadic::Type::kUint);
      writer_->AppendString(kActionNames[value.uint_value]);
    });
    writer_->AppendString("]");
    return;
  } else if (event_name_ == "softirq_exit") {
    using SIX = protos::pbzero::SoftirqExitFtraceEvent;
    WriteArgForField(SIX::kVecFieldNumber, DVW());
    writer_->AppendString(" [action=");
    WriteValueForField(SIX::kVecFieldNumber, [this](const Variadic& value) {
      PERFETTO_DCHECK(value.type == Variadic::Type::kUint);
      writer_->AppendString(kActionNames[value.uint_value]);
    });
    writer_->AppendString("]");
    return;
  } else if (event_name_ == "tracing_mark_write") {
    using TMW = protos::pbzero::TracingMarkWriteFtraceEvent;
    WriteValueForField(TMW::kTraceBeginFieldNumber,
                       [this](const Variadic& value) {
                         PERFETTO_DCHECK(value.type == Variadic::Type::kUint);
                         writer_->AppendChar(value.uint_value ? 'B' : 'E');
                       });
    writer_->AppendString("|");
    WriteValueForField(TMW::kPidFieldNumber, DVW());
    writer_->AppendString("|");
    WriteValueForField(TMW::kTraceNameFieldNumber, DVW());
    return;
  } else if (event_name_ == "dpu_tracing_mark_write") {
    using TMW = protos::pbzero::DpuTracingMarkWriteFtraceEvent;
    WriteValueForField(TMW::kTypeFieldNumber, [this](const Variadic& value) {
      PERFETTO_DCHECK(value.type == Variadic::Type::kUint);
      writer_->AppendChar(static_cast<char>(value.uint_value));
    });
    writer_->AppendString("|");
    WriteValueForField(TMW::kPidFieldNumber, DVW());
    writer_->AppendString("|");
    WriteValueForField(TMW::kNameFieldNumber, DVW());
    writer_->AppendString("|");
    WriteValueForField(TMW::kValueFieldNumber, DVW());
    return;
  } else if (event_name_ == "panel_write_generic") {
    using TMW = protos::pbzero::PanelWriteGenericFtraceEvent;
    WriteValueForField(TMW::kTypeFieldNumber, [this](const Variadic& value) {
      PERFETTO_DCHECK(value.type == Variadic::Type::kUint);
      writer_->AppendChar(static_cast<char>(value.uint_value));
    });
    writer_->AppendString("|");
    WriteValueForField(TMW::kPidFieldNumber, DVW());
    writer_->AppendString("|");
    WriteValueForField(TMW::kNameFieldNumber, DVW());
    writer_->AppendString("|");
    WriteValueForField(TMW::kValueFieldNumber, DVW());
    return;
  } else if (event_name_ == "g2d_tracing_mark_write") {
    using TMW = protos::pbzero::G2dTracingMarkWriteFtraceEvent;
    WriteValueForField(TMW::kTypeFieldNumber, [this](const Variadic& value) {
      PERFETTO_DCHECK(value.type == Variadic::Type::kUint);
      writer_->AppendChar(static_cast<char>(value.uint_value));
    });
    writer_->AppendString("|");
    WriteValueForField(TMW::kPidFieldNumber, DVW());
    writer_->AppendString("|");
    WriteValueForField(TMW::kNameFieldNumber, DVW());
    writer_->AppendString("|");
    WriteValueForField(TMW::kValueFieldNumber, DVW());
    return;
  } else if (event_name_ == "samsung_tracing_mark_write") {
    using TMW = protos::pbzero::SamsungTracingMarkWriteFtraceEvent;
    WriteValueForField(
        TMW::kTraceTypeFieldNumber, [this](const Variadic& value) {
          PERFETTO_DCHECK(value.type == Variadic::Type::kUint);
          writer_->AppendChar(static_cast<char>(value.uint_value));
        });
    writer_->AppendString("|");
    WriteValueForField(TMW::kPidFieldNumber, DVW());
    writer_->AppendString("|");
    WriteValueForField(TMW::kTraceNameFieldNumber, DVW());
    writer_->AppendString("|");
    WriteValueForField(TMW::kValueFieldNumber, DVW());
    return;
  } else if (event_name_ == "cgroup_attach_task") {
    using CAT = protos::pbzero::CgroupAttachTaskFtraceEvent;
    WriteArgForField(CAT::kDstRootFieldNumber, DVW());
    WriteArgForField(CAT::kDstIdFieldNumber, DVW());
    WriteArgForField(CAT::kCnameFieldNumber, "cgroup", DVW());
    WriteArgForField(CAT::kDstLevelFieldNumber, DVW());
    WriteArgForField(CAT::kDstPathFieldNumber, DVW());
    WriteArgForField(CAT::kPidFieldNumber, DVW());
    WriteArgForField(CAT::kCommFieldNumber, DVW());
    return;
  } else if (event_name_ == "hrtimer_expire_entry") {
    using HEE = protos::pbzero::HrtimerExpireEntryFtraceEvent;
    WriteArgForField(HEE::kFunctionFieldNumber,
                     Wrap(&ArgsSerializer::WriteKernelFnValue));
    return;
  }
  for (; !cursor_->Eof(); cursor_->Next()) {
    WriteArgAtRow(cursor_->ToRowNumber().row_number(), DVW());
  }
}

void ArgsSerializer::WriteArg(base::StringView key,
                              Variadic value,
                              const ValueWriter& writer) {
  writer_->AppendChar(' ');
  writer_->AppendString(key.data(), key.size());
  writer_->AppendChar('=');

  if (key == "gfp_flags") {
    auto kernel_version =
        SystemInfoTracker::GetOrCreate(context_)->GetKernelVersion();
    WriteGfpFlag(value.uint_value, kernel_version, writer_);
    return;
  }
  writer(value);
}

void ArgsSerializer::WriteValue(const Variadic& value) {
  switch (value.type) {
    case Variadic::kInt:
      writer_->AppendInt(value.int_value);
      break;
    case Variadic::kUint:
      writer_->AppendUnsignedInt(value.uint_value);
      break;
    case Variadic::kString: {
      const auto& str = storage_->GetString(value.string_value);
      writer_->AppendString(str.c_str(), str.size());
      break;
    }
    case Variadic::kReal:
      writer_->AppendDouble(value.real_value);
      break;
    case Variadic::kPointer:
      writer_->AppendUnsignedInt(value.pointer_value);
      break;
    case Variadic::kBool:
      writer_->AppendBool(value.bool_value);
      break;
    case Variadic::kJson: {
      const auto& str = storage_->GetString(value.json_value);
      writer_->AppendString(str.c_str(), str.size());
      break;
    }
    case Variadic::kNull:
      writer_->AppendLiteral("[NULL]");
      break;
  }
}

}  // namespace

void ToFtrace::Step(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
  PERFETTO_DCHECK(argc == 1);

  if (sqlite::value::Type(argv[0]) != sqlite::Type::kInteger) {
    return sqlite::utils::SetError(ctx, "Usage: to_ftrace(id)");
  }

  auto* context = GetUserData(ctx);
  uint32_t row = static_cast<uint32_t>(sqlite::value::Int64(argv[0]));

  auto str = context->serializer.SerializeToString(row);
  if (str.get() == nullptr) {
    return sqlite::utils::SetError(
        ctx, base::ErrStatus("to_ftrace: Cannot serialize row id %u", row));
  }

  sqlite::result::TransientString(ctx, str.get());
}

SystraceSerializer::SystraceSerializer(TraceProcessorContext* context)
    : storage_(context->storage.get()),
      context_(context),
      cursor_(storage_->arg_table().CreateCursor({
          dataframe::FilterSpec{
              tables::ArgTable::ColumnIndex::arg_set_id,
              0,
              dataframe::Eq{},
              std::nullopt,
          },
      })) {}

SystraceSerializer::ScopedCString SystraceSerializer::SerializeToString(
    uint32_t raw_row) {
  const auto& raw = storage_->ftrace_event_table();

  char line[4096];
  base::FixedStringWriter writer(line, sizeof(line));

  auto row = raw[raw_row];
  StringId event_name_id = row.name();
  NullTermStringView event_name = storage_->GetString(event_name_id);
  if (event_name.StartsWith("chrome_event.") ||
      event_name.StartsWith("track_event.")) {
    return ScopedCString(nullptr, nullptr);
  }

  SerializePrefix(raw_row, &writer);

  writer.AppendChar(' ');
  if (event_name == "print" || event_name == "g2d_tracing_mark_write" ||
      event_name == "dpu_tracing_mark_write") {
    writer.AppendString("tracing_mark_write");
  } else {
    writer.AppendString(event_name.c_str(), event_name.size());
  }
  writer.AppendChar(':');

  ArgsSerializer serializer(context_, row.arg_set_id(), &cursor_, event_name,
                            &proto_id_to_arg_index_by_event_[event_name_id],
                            &writer);
  serializer.SerializeArgs();

  return {writer.CreateStringCopy(), free};
}

void SystraceSerializer::SerializePrefix(uint32_t raw_row,
                                         base::FixedStringWriter* writer) {
  const auto& raw = storage_->ftrace_event_table();
  const auto& cpu_table = storage_->cpu_table();

  auto row = raw[raw_row];
  int64_t ts = row.ts();
  auto ucpu = row.ucpu();
  auto cpu = cpu_table[ucpu.value].cpu();

  auto thread_row = storage_->thread_table()[row.utid()];
  int64_t tid = thread_row.tid();

  int64_t tgid = 0;
  auto opt_upid = thread_row.upid();
  if (opt_upid.has_value()) {
    tgid = storage_->process_table()[*opt_upid].pid();
  }
  auto name = context_->storage->GetString(thread_row.name());
  FtraceTime ftrace_time(ts);
  if (tid == 0) {
    name = "<idle>";
  } else if (name.empty()) {
    name = "<unknown>";
  }

  int64_t padding = 16 - static_cast<int64_t>(name.size());
  if (padding > 0) {
    writer->AppendChar(' ', static_cast<size_t>(padding));
  }
  for (size_t i = 0; i < name.size(); ++i) {
    char c = name.data()[i];
    writer->AppendChar(c == '-' ? '_' : c);
  }
  writer->AppendChar('-');

  size_t pre_pid_pos = writer->pos();
  writer->AppendInt(tid);
  size_t pid_chars = writer->pos() - pre_pid_pos;
  if (PERFETTO_LIKELY(pid_chars < 5)) {
    writer->AppendChar(' ', 5 - pid_chars);
  }

  writer->AppendLiteral(" (");
  if (tgid == 0) {
    writer->AppendLiteral("-----");
  } else {
    writer->AppendPaddedInt<' ', 5>(tgid);
  }
  writer->AppendLiteral(") [");
  writer->AppendPaddedInt<'0', 3>(cpu ? *cpu : 0);
  writer->AppendLiteral("] .... ");

  writer->AppendInt(ftrace_time.secs);
  writer->AppendChar('.');
  writer->AppendPaddedInt<'0', 6>(ftrace_time.micros);
  writer->AppendChar(':');
}

}  // namespace perfetto::trace_processor
