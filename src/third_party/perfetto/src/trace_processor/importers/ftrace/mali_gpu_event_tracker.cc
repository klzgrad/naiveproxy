/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "src/trace_processor/importers/ftrace/mali_gpu_event_tracker.h"

#include <cstdint>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/protozero/field.h"
#include "protos/perfetto/trace/ftrace/ftrace_event.pbzero.h"
#include "src/trace_processor/importers/common/slice_tracker.h"
#include "src/trace_processor/importers/common/track_compressor.h"
#include "src/trace_processor/importers/common/track_tracker.h"
#include "src/trace_processor/importers/common/tracks.h"
#include "src/trace_processor/importers/common/tracks_common.h"
#include "src/trace_processor/importers/common/tracks_internal.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/variadic.h"

#include "protos/perfetto/trace/ftrace/mali.pbzero.h"

namespace perfetto::trace_processor {
namespace {

constexpr auto kMaliIrqBlueprint = tracks::SliceBlueprint(
    "cpu_mali_irq",
    tracks::DimensionBlueprints(tracks::kCpuDimensionBlueprint),
    tracks::FnNameBlueprint([](uint32_t cpu) {
      return base::StackString<255>("Mali Irq Cpu %u", cpu);
    }));

}  // namespace

namespace {

constexpr auto kMcuStateBlueprint = tracks::SliceBlueprint("mali_mcu_state");

}

MaliGpuEventTracker::MaliGpuEventTracker(TraceProcessorContext* context)
    : context_(context),
      mali_KCPU_CQS_SET_id_(
          context->storage->InternString("mali_KCPU_CQS_SET")),
      mali_KCPU_CQS_WAIT_id_(
          context->storage->InternString("mali_KCPU_CQS_WAIT")),
      mali_KCPU_FENCE_SIGNAL_id_(
          context->storage->InternString("mali_KCPU_FENCE_SIGNAL")),
      mali_KCPU_FENCE_WAIT_id_(
          context->storage->InternString("mali_KCPU_FENCE_WAIT")),
      mali_CSF_INTERRUPT_id_(
          context->storage->InternString("mali_CSF_INTERRUPT")),
      mali_CSF_INTERRUPT_info_val_id_(
          context->storage->InternString("info_val")),
      current_mcu_state_name_(kNullStringId) {
  using protos::pbzero::FtraceEvent;

  mcu_state_names_.fill(kNullStringId);
  RegisterMcuState<
      FtraceEvent::kMaliMaliPMMCUHCTLCORESDOWNSCALENOTIFYPENDFieldNumber>(
      "HCTL_CORES_DOWN_SCALE_NOTIFY_PEND");
  RegisterMcuState<FtraceEvent::kMaliMaliPMMCUHCTLCORESNOTIFYPENDFieldNumber>(
      "HCTL_CORES_NOTIFY_PEND");
  RegisterMcuState<FtraceEvent::kMaliMaliPMMCUHCTLCOREINACTIVEPENDFieldNumber>(
      "HCTL_CORE_INACTIVE_PEND");
  RegisterMcuState<FtraceEvent::kMaliMaliPMMCUHCTLMCUONRECHECKFieldNumber>(
      "HCTL_MCU_ON_RECHECK");
  RegisterMcuState<
      FtraceEvent::kMaliMaliPMMCUHCTLSHADERSCOREOFFPENDFieldNumber>(
      "HCTL_SHADERS_CORE_OFF_PEND");
  RegisterMcuState<FtraceEvent::kMaliMaliPMMCUHCTLSHADERSPENDOFFFieldNumber>(
      "HCTL_SHADERS_PEND_OFF");
  RegisterMcuState<FtraceEvent::kMaliMaliPMMCUHCTLSHADERSPENDONFieldNumber>(
      "HCTL_SHADERS_PEND_ON");
  RegisterMcuState<FtraceEvent::kMaliMaliPMMCUHCTLSHADERSREADYOFFFieldNumber>(
      "HCTL_SHADERS_READY_OFF");
  RegisterMcuState<FtraceEvent::kMaliMaliPMMCUINSLEEPFieldNumber>("IN_SLEEP");
  RegisterMcuState<FtraceEvent::kMaliMaliPMMCUOFFFieldNumber>("OFF");
  RegisterMcuState<FtraceEvent::kMaliMaliPMMCUONFieldNumber>("ON");
  RegisterMcuState<FtraceEvent::kMaliMaliPMMCUONCOREATTRUPDATEPENDFieldNumber>(
      "ON_CORE_ATTR_UPDATE_PEND");
  RegisterMcuState<FtraceEvent::kMaliMaliPMMCUONGLBREINITPENDFieldNumber>(
      "ON_GLB_REINIT_PEND");
  RegisterMcuState<FtraceEvent::kMaliMaliPMMCUONHALTFieldNumber>("ON_HALT");
  RegisterMcuState<FtraceEvent::kMaliMaliPMMCUONHWCNTDISABLEFieldNumber>(
      "ON_HWCNT_DISABLE");
  RegisterMcuState<FtraceEvent::kMaliMaliPMMCUONHWCNTENABLEFieldNumber>(
      "ON_HWCNT_ENABLE");
  RegisterMcuState<FtraceEvent::kMaliMaliPMMCUONPENDHALTFieldNumber>(
      "ON_PEND_HALT");
  RegisterMcuState<FtraceEvent::kMaliMaliPMMCUONPENDSLEEPFieldNumber>(
      "ON_PEND_SLEEP");
  RegisterMcuState<FtraceEvent::kMaliMaliPMMCUONSLEEPINITIATEFieldNumber>(
      "ON_SLEEP_INITIATE");
  RegisterMcuState<FtraceEvent::kMaliMaliPMMCUPENDOFFFieldNumber>("PEND_OFF");
  RegisterMcuState<FtraceEvent::kMaliMaliPMMCUPENDONRELOADFieldNumber>(
      "PEND_ON_RELOAD");
  RegisterMcuState<FtraceEvent::kMaliMaliPMMCUPOWERDOWNFieldNumber>(
      "POWER_DOWN");
  RegisterMcuState<FtraceEvent::kMaliMaliPMMCURESETWAITFieldNumber>(
      "RESET_WAIT");
}

template <uint32_t FieldId>
void MaliGpuEventTracker::RegisterMcuState(const char* state_name) {
  static_assert(FieldId >= kFirstMcuStateId && FieldId <= kLastMcuStateId);
  mcu_state_names_[FieldId - kFirstMcuStateId] =
      context_->storage->InternString(state_name);
}

void MaliGpuEventTracker::ParseMaliGpuIrqEvent(int64_t ts,
                                               uint32_t field_id,
                                               uint32_t cpu,
                                               protozero::ConstBytes blob) {
  // Since these events are called from an interrupt context they cannot be
  // associated to a single process or thread. Add to a custom Mali Irq track
  // instead.
  TrackId track_id = context_->track_tracker->InternTrack(
      kMaliIrqBlueprint, tracks::Dimensions(cpu));

  switch (field_id) {
    case protos::pbzero::FtraceEvent::kMaliMaliCSFINTERRUPTSTARTFieldNumber: {
      ParseMaliCSFInterruptStart(ts, track_id, blob);
      break;
    }
    case protos::pbzero::FtraceEvent::kMaliMaliCSFINTERRUPTENDFieldNumber: {
      ParseMaliCSFInterruptEnd(ts, track_id, blob);
      break;
    }
    default:
      PERFETTO_DFATAL("Unexpected field id");
      break;
  }
}

void MaliGpuEventTracker::ParseMaliGpuMcuStateEvent(int64_t timestamp,
                                                    uint32_t field_id) {
  if (field_id < kFirstMcuStateId || field_id > kLastMcuStateId) {
    PERFETTO_FATAL("Mali MCU state ID out of range");
  }

  StringId state_name = mcu_state_names_[field_id - kFirstMcuStateId];
  if (state_name == kNullStringId) {
    context_->storage->IncrementStats(stats::mali_unknown_mcu_state_id);
    return;
  }

  TrackId track_id = context_->track_tracker->InternTrack(kMcuStateBlueprint);
  if (current_mcu_state_name_ != kNullStringId) {
    context_->slice_tracker->End(timestamp, track_id, kNullStringId,
                                 current_mcu_state_name_);
  }

  context_->slice_tracker->Begin(timestamp, track_id, kNullStringId,
                                 state_name);
  current_mcu_state_name_ = state_name;
}

void MaliGpuEventTracker::ParseMaliCSFInterruptStart(
    int64_t timestamp,
    TrackId track_id,
    protozero::ConstBytes blob) {
  protos::pbzero::MaliMaliCSFINTERRUPTSTARTFtraceEvent::Decoder evt(blob);
  context_->slice_tracker->Begin(
      timestamp, track_id, kNullStringId, mali_CSF_INTERRUPT_id_,
      [&, this](ArgsTracker::BoundInserter* inserter) {
        inserter->AddArg(mali_CSF_INTERRUPT_info_val_id_,
                         Variadic::UnsignedInteger(evt.info_val()));
      });
}

void MaliGpuEventTracker::ParseMaliCSFInterruptEnd(int64_t timestamp,
                                                   TrackId track_id,
                                                   protozero::ConstBytes blob) {
  protos::pbzero::MaliMaliCSFINTERRUPTSTARTFtraceEvent::Decoder evt(blob);

  context_->slice_tracker->End(
      timestamp, track_id, kNullStringId, mali_CSF_INTERRUPT_id_,
      [&, this](ArgsTracker::BoundInserter* inserter) {
        inserter->AddArg(mali_CSF_INTERRUPT_info_val_id_,
                         Variadic::UnsignedInteger(evt.info_val()));
      });
}
}  // namespace perfetto::trace_processor
