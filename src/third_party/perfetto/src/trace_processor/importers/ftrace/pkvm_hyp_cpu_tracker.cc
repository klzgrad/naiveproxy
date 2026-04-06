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

#include "src/trace_processor/importers/ftrace/pkvm_hyp_cpu_tracker.h"

#include <cstdint>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/protozero/field.h"
#include "src/trace_processor/importers/common/slice_tracker.h"
#include "src/trace_processor/importers/common/track_tracker.h"
#include "src/trace_processor/importers/common/tracks.h"
#include "src/trace_processor/importers/common/tracks_common.h"
#include "src/trace_processor/storage/trace_storage.h"

#include "protos/perfetto/trace/ftrace/ftrace_event.pbzero.h"
#include "protos/perfetto/trace/ftrace/hyp.pbzero.h"
#include "protos/perfetto/trace/ftrace/hypervisor.pbzero.h"
#include "src/trace_processor/types/variadic.h"

namespace perfetto::trace_processor {
namespace {

constexpr auto kPkvmBlueprint = tracks::SliceBlueprint(
    "pkvm_hypervisor",
    tracks::DimensionBlueprints(tracks::kCpuDimensionBlueprint),
    tracks::FnNameBlueprint([](uint32_t cpu) {
      return base::StackString<255>("pkVM Hypervisor CPU %u", cpu);
    }));

}  // namespace

PkvmHypervisorCpuTracker::PkvmHypervisorCpuTracker(
    TraceProcessorContext* context)
    : context_(context),
      category_(context->storage->InternString("pkvm_hyp")),
      slice_name_(context->storage->InternString("hyp")),
      hyp_enter_reason_(context->storage->InternString("hyp_enter_reason")),
      func_id_(context_->storage->InternString("func_id")),
      handled_(context_->storage->InternString("handled")),
      err_(context_->storage->InternString("err")),
      host_ffa_call_(context_->storage->InternString("host_ffa_call")),
      iommu_idmap_(context_->storage->InternString("iommu_idmap")),
      from_(context_->storage->InternString("from")),
      to_(context_->storage->InternString("to")),
      prot_(context_->storage->InternString("prot")),
      psci_mem_protect_(context_->storage->InternString("psci_mem_protect")),
      count_(context_->storage->InternString("count")),
      was_(context_->storage->InternString("was")),
      iommu_idmap_complete_(
          context_->storage->InternString("iommu_idmap_complete")),
      map_(context_->storage->InternString("map")),
      vcpu_illegal_trap_(context_->storage->InternString("vcpu_illegal_trap")),
      esr_(context_->storage->InternString("esr")),
      host_hcall_(context_->storage->InternString("host_hcall")),
      id_(context_->storage->InternString("id")),
      invalid_(context_->storage->InternString("invalid")),
      host_smc_(context_->storage->InternString("host_smc")),
      forwarded_(context_->storage->InternString("forwarded")),
      host_mem_abort_(context_->storage->InternString("host_mem_abort")),
      addr_(context_->storage->InternString("addr")) {}

// static
bool PkvmHypervisorCpuTracker::IsPkvmHypervisorEvent(uint32_t event_id) {
  using protos::pbzero::FtraceEvent;
  switch (event_id) {
    case FtraceEvent::kHypEnterFieldNumber:
    case FtraceEvent::kHypervisorHypEnterFieldNumber:
    case FtraceEvent::kHypExitFieldNumber:
    case FtraceEvent::kHypervisorHypExitFieldNumber:
    case FtraceEvent::kHostHcallFieldNumber:
    case FtraceEvent::kHypervisorHostHcallFieldNumber:
    case FtraceEvent::kHostMemAbortFieldNumber:
    case FtraceEvent::kHypervisorHostMemAbortFieldNumber:
    case FtraceEvent::kHostSmcFieldNumber:
    case FtraceEvent::kHypervisorHostSmcFieldNumber:
    case FtraceEvent::kHostFfaCallFieldNumber:
    case FtraceEvent::kIommuIdmapFieldNumber:
    case FtraceEvent::kHypervisorIommuIdmapFieldNumber:
    case FtraceEvent::kPsciMemProtectFieldNumber:
    case FtraceEvent::kHypervisorPsciMemProtectFieldNumber:
    case FtraceEvent::kHypervisorIommuIdmapCompleteFieldNumber:
    case FtraceEvent::kHypervisorVcpuIllegalTrapFieldNumber:
      return true;
    default:
      return false;
  }
}

void PkvmHypervisorCpuTracker::ParseHypEvent(uint32_t cpu,
                                             int64_t timestamp,
                                             uint32_t event_id,
                                             protozero::ConstBytes blob) {
  using protos::pbzero::FtraceEvent;
  switch (event_id) {
    case FtraceEvent::kHypEnterFieldNumber:
    case FtraceEvent::kHypervisorHypEnterFieldNumber:
      ParseHypEnter(cpu, timestamp);
      break;
    case FtraceEvent::kHypExitFieldNumber:
    case FtraceEvent::kHypervisorHypExitFieldNumber:
      ParseHypExit(cpu, timestamp);
      break;
    case FtraceEvent::kHostHcallFieldNumber:
    case FtraceEvent::kHypervisorHostHcallFieldNumber:
      ParseHostHcall(cpu, blob);
      break;
    case FtraceEvent::kHostMemAbortFieldNumber:
    case FtraceEvent::kHypervisorHostMemAbortFieldNumber:
      ParseHostMemAbort(cpu, blob);
      break;
    case FtraceEvent::kHostSmcFieldNumber:
    case FtraceEvent::kHypervisorHostSmcFieldNumber:
      ParseHostSmc(cpu, blob);
      break;
    case FtraceEvent::kHostFfaCallFieldNumber:
      ParseHostFfaCall(cpu, blob);
      break;
    case FtraceEvent::kIommuIdmapFieldNumber:
    case FtraceEvent::kHypervisorIommuIdmapFieldNumber:
      ParseIommuIdmap(cpu, blob);
      break;
    case FtraceEvent::kPsciMemProtectFieldNumber:
    case FtraceEvent::kHypervisorPsciMemProtectFieldNumber:
      ParsePsciMemProtect(cpu, blob);
      break;
    case FtraceEvent::kHypervisorIommuIdmapCompleteFieldNumber:
      ParseIommuIdmapComplete(cpu, blob);
      break;
    case FtraceEvent::kHypervisorVcpuIllegalTrapFieldNumber:
      ParseVcuIllegalTrap(cpu, blob);
      break;
    // TODO(b/249050813): add remaining hypervisor events
    default:
      PERFETTO_FATAL("Not a hypervisor event %u", event_id);
  }
}

void PkvmHypervisorCpuTracker::ParseHypEnter(uint32_t cpu, int64_t timestamp) {
  // TODO(b/249050813): handle bad events (e.g. 2 hyp_enter in a row)
  TrackId track_id = context_->track_tracker->InternTrack(
      kPkvmBlueprint, tracks::Dimensions(cpu));
  context_->slice_tracker->Begin(timestamp, track_id, category_, slice_name_);
}

void PkvmHypervisorCpuTracker::ParseHypExit(uint32_t cpu, int64_t timestamp) {
  // TODO(b/249050813): handle bad events (e.g. 2 hyp_exit in a row)
  TrackId track_id = context_->track_tracker->InternTrack(
      kPkvmBlueprint, tracks::Dimensions(cpu));
  context_->slice_tracker->End(timestamp, track_id);
}

void PkvmHypervisorCpuTracker::ParseHostHcall(uint32_t cpu,
                                              protozero::ConstBytes blob) {
  protos::pbzero::HostHcallFtraceEvent::Decoder evt(blob);
  TrackId track_id = context_->track_tracker->InternTrack(
      kPkvmBlueprint, tracks::Dimensions(cpu));
  context_->slice_tracker->AddArgs(
      track_id, category_, slice_name_,
      [&, this](ArgsTracker::BoundInserter* inserter) {
        inserter->AddArg(hyp_enter_reason_, Variadic::String(host_hcall_));
        inserter->AddArg(id_, Variadic::UnsignedInteger(evt.id()));
        inserter->AddArg(invalid_, Variadic::UnsignedInteger(evt.invalid()));
      });
}

void PkvmHypervisorCpuTracker::ParseHostSmc(uint32_t cpu,
                                            protozero::ConstBytes blob) {
  protos::pbzero::HostSmcFtraceEvent::Decoder evt(blob);
  TrackId track_id = context_->track_tracker->InternTrack(
      kPkvmBlueprint, tracks::Dimensions(cpu));
  context_->slice_tracker->AddArgs(
      track_id, category_, slice_name_,
      [&, this](ArgsTracker::BoundInserter* inserter) {
        inserter->AddArg(hyp_enter_reason_, Variadic::String(host_smc_));
        inserter->AddArg(id_, Variadic::UnsignedInteger(evt.id()));
        inserter->AddArg(forwarded_,
                         Variadic::UnsignedInteger(evt.forwarded()));
      });
}

void PkvmHypervisorCpuTracker::ParseHostMemAbort(uint32_t cpu,
                                                 protozero::ConstBytes blob) {
  protos::pbzero::HostMemAbortFtraceEvent::Decoder evt(blob);
  TrackId track_id = context_->track_tracker->InternTrack(
      kPkvmBlueprint, tracks::Dimensions(cpu));
  context_->slice_tracker->AddArgs(
      track_id, category_, slice_name_,
      [&, this](ArgsTracker::BoundInserter* inserter) {
        inserter->AddArg(hyp_enter_reason_, Variadic::String(host_mem_abort_));
        inserter->AddArg(esr_, Variadic::UnsignedInteger(evt.esr()));
        inserter->AddArg(addr_, Variadic::UnsignedInteger(evt.addr()));
      });
}

void PkvmHypervisorCpuTracker::ParseHostFfaCall(uint32_t cpu,
                                                protozero::ConstBytes blob) {
  protos::pbzero::HostFfaCallFtraceEvent::Decoder evt(blob);
  TrackId track_id = context_->track_tracker->InternTrack(
      kPkvmBlueprint, tracks::Dimensions(cpu));
  context_->slice_tracker->AddArgs(
      track_id, category_, slice_name_,
      [&, this](ArgsTracker::BoundInserter* inserter) {
        inserter->AddArg(hyp_enter_reason_, Variadic::String(host_ffa_call_));
        inserter->AddArg(func_id_, Variadic::UnsignedInteger(evt.func_id()));
        inserter->AddArg(handled_, Variadic::Integer(evt.handled()));
        inserter->AddArg(err_, Variadic::Integer(evt.err()));
      });
}

void PkvmHypervisorCpuTracker::ParseIommuIdmap(uint32_t cpu,
                                               protozero::ConstBytes blob) {
  protos::pbzero::IommuIdmapFtraceEvent::Decoder evt(blob);
  TrackId track_id = context_->track_tracker->InternTrack(
      kPkvmBlueprint, tracks::Dimensions(cpu));
  context_->slice_tracker->AddArgs(
      track_id, category_, slice_name_,
      [&, this](ArgsTracker::BoundInserter* inserter) {
        inserter->AddArg(hyp_enter_reason_, Variadic::String(iommu_idmap_));
        inserter->AddArg(from_, Variadic::UnsignedInteger(evt.from()));
        inserter->AddArg(to_, Variadic::UnsignedInteger(evt.to()));
        inserter->AddArg(prot_, Variadic::Integer(evt.prot()));
      });
}

void PkvmHypervisorCpuTracker::ParsePsciMemProtect(uint32_t cpu,
                                                   protozero::ConstBytes blob) {
  protos::pbzero::PsciMemProtectFtraceEvent::Decoder evt(blob);
  TrackId track_id = context_->track_tracker->InternTrack(
      kPkvmBlueprint, tracks::Dimensions(cpu));
  context_->slice_tracker->AddArgs(
      track_id, category_, slice_name_,
      [&, this](ArgsTracker::BoundInserter* inserter) {
        inserter->AddArg(hyp_enter_reason_,
                         Variadic::String(psci_mem_protect_));
        inserter->AddArg(count_, Variadic::UnsignedInteger(evt.count()));
        inserter->AddArg(was_, Variadic::UnsignedInteger(evt.was()));
      });
}

void PkvmHypervisorCpuTracker::ParseIommuIdmapComplete(
    uint32_t cpu,
    protozero::ConstBytes blob) {
  protos::pbzero::HypervisorIommuIdmapCompleteFtraceEvent::Decoder evt(blob);
  TrackId track_id = context_->track_tracker->InternTrack(
      kPkvmBlueprint, tracks::Dimensions(cpu));
  context_->slice_tracker->AddArgs(
      track_id, category_, slice_name_,
      [&, this](ArgsTracker::BoundInserter* inserter) {
        inserter->AddArg(hyp_enter_reason_,
                         Variadic::String(iommu_idmap_complete_));
        inserter->AddArg(map_, Variadic::Boolean(evt.map()));
      });
}

void PkvmHypervisorCpuTracker::ParseVcuIllegalTrap(uint32_t cpu,
                                                   protozero::ConstBytes blob) {
  protos::pbzero::HypervisorVcpuIllegalTrapFtraceEvent::Decoder evt(blob);
  TrackId track_id = context_->track_tracker->InternTrack(
      kPkvmBlueprint, tracks::Dimensions(cpu));
  context_->slice_tracker->AddArgs(
      track_id, category_, slice_name_,
      [&, this](ArgsTracker::BoundInserter* inserter) {
        inserter->AddArg(hyp_enter_reason_,
                         Variadic::String(vcpu_illegal_trap_));
        inserter->AddArg(esr_, Variadic::UnsignedInteger(evt.esr()));
      });
}

}  // namespace perfetto::trace_processor
