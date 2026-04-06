/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "src/trace_processor/importers/etm/etm_v4_stream_demultiplexer.h"

#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <utility>

#include "perfetto/base/flat_set.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/importers/etm/etm_tracker.h"
#include "src/trace_processor/importers/etm/etm_v4_stream.h"
#include "src/trace_processor/importers/etm/frame_decoder.h"
#include "src/trace_processor/importers/etm/opencsd.h"
#include "src/trace_processor/importers/etm/types.h"
#include "src/trace_processor/importers/perf/aux_data_tokenizer.h"
#include "src/trace_processor/importers/perf/aux_stream_manager.h"
#include "src/trace_processor/importers/perf/auxtrace_info_record.h"
#include "src/trace_processor/importers/perf/perf_event.h"
#include "src/trace_processor/importers/perf/reader.h"
#include "src/trace_processor/importers/perf/util.h"
#include "src/trace_processor/tables/etm_tables_py.h"

// Be aware the in the OSCD namespace an ETM chunk is an ETM trace.
namespace perfetto::trace_processor::etm {
namespace {

static constexpr uint64_t kEtmV4Magic = 0x4040404040404040ULL;
static constexpr uint64_t kEteMagic = 0x5050505050505050ULL;

struct RawHeader {
  uint64_t version;
  uint32_t cpu_count;
  uint32_t pmu_type;
  uint64_t snapshot;
};

struct RawCpuHeader {
  uint64_t magic;
  uint64_t cpu;
  uint64_t trace_parameter_count;
};

struct RawEtmV4Info {
  uint64_t trcconfigr;
  uint64_t trctraceidr;
  uint64_t trcidr0;
  uint64_t trcidr1;
  uint64_t trcidr2;
  uint64_t trcidr8;
  uint64_t trcauthstatus;
};

struct RawEteInfo : public RawEtmV4Info {
  uint64_t trcdevarch;
};

base::StatusOr<std::unique_ptr<Configuration>> ParseEtmV4(TraceBlobView blob) {
  perf_importer::Reader reader(std::move(blob));

  RawEtmV4Info info;
  if (!reader.Read(info)) {
    return base::ErrStatus("Failed to read EtmV4Info");
  }

  ocsd_etmv4_cfg cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.reg_idr0 = static_cast<uint32_t>(info.trcidr0);
  cfg.reg_idr1 = static_cast<uint32_t>(info.trcidr1);
  cfg.reg_idr2 = static_cast<uint32_t>(info.trcidr2);
  cfg.reg_idr8 = static_cast<uint32_t>(info.trcidr8);
  cfg.reg_idr9 = 0;
  cfg.reg_idr10 = 0;
  cfg.reg_idr11 = 0;
  cfg.reg_idr12 = 0;
  cfg.reg_idr13 = 0;
  cfg.reg_configr = static_cast<uint32_t>(info.trcconfigr);
  cfg.reg_traceidr = static_cast<uint32_t>(info.trctraceidr);
  // For minor_version >= 4 we can assume ARCH_AA64
  cfg.arch_ver = ((cfg.reg_idr0 >> 4) & 0x0F) >= 4 ? ARCH_AA64 : ARCH_V8;
  cfg.core_prof = profile_CortexA;

  return std::make_unique<Configuration>(cfg);
}

base::StatusOr<std::unique_ptr<Configuration>> ParseEte(TraceBlobView blob) {
  perf_importer::Reader reader(std::move(blob));

  RawEteInfo info;
  if (!reader.Read(info)) {
    return base::ErrStatus("Failed to read RawEteInfo");
  }

  ocsd_ete_cfg cfg;
  cfg.reg_idr0 = static_cast<uint32_t>(info.trcidr0);
  cfg.reg_idr1 = static_cast<uint32_t>(info.trcidr1);
  cfg.reg_idr2 = static_cast<uint32_t>(info.trcidr2);
  cfg.reg_idr8 = static_cast<uint32_t>(info.trcidr8);
  cfg.reg_configr = static_cast<uint32_t>(info.trcconfigr);
  cfg.reg_traceidr = static_cast<uint32_t>(info.trctraceidr);
  cfg.reg_devarch = static_cast<uint32_t>(info.trcdevarch);
  cfg.arch_ver = ARCH_AA64;
  cfg.core_prof = profile_CortexA;

  return std::make_unique<Configuration>(cfg);
}

base::StatusOr<std::pair<uint32_t, std::unique_ptr<Configuration>>>
ReadCpuConfig(perf_importer::Reader& reader) {
  RawCpuHeader cpu_header;
  if (!reader.Read(cpu_header)) {
    return base::ErrStatus("Failed to read ETM info header");
  }

  uint32_t cpu;
  if (!perf_importer::SafeCast(cpu_header.cpu, &cpu)) {
    return base::ErrStatus("Integer overflow in ETM info header");
  }

  uint32_t size;
  if (cpu_header.trace_parameter_count >
      std::numeric_limits<uint32_t>::max() / 8) {
    return base::ErrStatus("Integer overflow in ETM info header");
  }
  size = static_cast<uint32_t>(cpu_header.trace_parameter_count * 8);

  TraceBlobView blob;
  if (!reader.ReadBlob(blob, size)) {
    return base::ErrStatus(
        "Not enough data in ETM info. trace_parameter_count=%" PRIu64,
        cpu_header.trace_parameter_count);
  }

  std::unique_ptr<Configuration> config;

  switch (cpu_header.magic) {
    case kEtmV4Magic: {
      ASSIGN_OR_RETURN(config, ParseEtmV4(std::move(blob)));
      break;
    }

    case kEteMagic: {
      ASSIGN_OR_RETURN(config, ParseEte(std::move(blob)));
      break;
    }

    default:
      return base::ErrStatus("Unknown magic: 0x%" PRIX64, cpu_header.magic);
  }
  return std::make_pair(cpu, std::move(config));
}

base::StatusOr<PerCpuConfiguration> ParseAuxtraceInfo(
    perf_importer::AuxtraceInfoRecord info) {
  PERFETTO_CHECK(info.type == PERF_AUXTRACE_CS_ETM);
  perf_importer::Reader reader(std::move(info.payload));

  RawHeader header;
  if (!reader.Read(header)) {
    return base::ErrStatus("Failed to read ETM info header");
  }

  if (header.version < 1) {
    return base::ErrStatus("Unsupported version in EtmConfiguration: %" PRIu64,
                           header.version);
  }

  PerCpuConfiguration per_cpu_configuration;
  base::FlatSet<uint8_t> seen_trace_ids;
  for (; header.cpu_count != 0; --header.cpu_count) {
    ASSIGN_OR_RETURN(auto cpu_config, ReadCpuConfig(reader));
    uint32_t cpu = cpu_config.first;
    std::unique_ptr<Configuration> config = std::move(cpu_config.second);

    // TODO(carlscab): support VMID
    if (!config->etm_v4_config().enabledCID()) {
      return base::ErrStatus(
          "ETM Stream without context ID not supported (yet?)");
    }

    const auto trace_id = config->etm_v4_config().getTraceID();
    if (!OCSD_IS_VALID_CS_SRC_ID(trace_id)) {
      return base::ErrStatus("Invalid trace id: %" PRIu8, trace_id);
    }
    if (seen_trace_ids.count(trace_id)) {
      return base::ErrStatus("Duplicate configuration for trace Id: %" PRIu8,
                             trace_id);
    }

    bool success = per_cpu_configuration.Insert(cpu, std::move(config)).second;

    if (!success) {
      return base::ErrStatus("Duplicate configuration for CPU Id: %" PRIu32,
                             cpu);
    }
  }

  return base::StatusOr<PerCpuConfiguration>(std::move(per_cpu_configuration));
}

// ETM data is embedded in the AUX buffers.
// Data can be stored in two different formats depending on whether ETR or TRBE
// is used to collect the data.
//
// In the former all CPUs write their data to the ETR and once trace is stopped
// on all CPUs it is written to system memory. Thus data for all CPUs arrives in
// one AUX record for the CPU that collected the data. The actual trace data
// will be in frame formatted form and needs to be passed to a decoder to
// extract the various streams. AUX data is passed by the perf importer to the
// CPU specific `AuxDataStream`, but as we just said we need to first decode
// this data to extract the real per CPU streams, so the `EtmV4Stream` classes
// (`AuxDataStream` subclasses) forward such data to this class, that will
// decode the streams and finally forward them back to the CPU specific
// `EtmV4Stream` where it can now be handled.
//
// For the TRBE the data that arrives in the AUX record is unformatted and is
// the data for that given CPU so it can be directly processed by the
// `EtmV4Stream` class without needing to decode it first.
//
// Data flow for framed data (ETR):
//   1. `PerfDataTokenizer` parses `AuxData` for cpu x and forwards it to the
//      `AuxDataStream` bound to that cpu
//   2. `EtmV4Stream` bound to cpu x determines AuxData is framed and forwards
//       it to the `FrameDecoder` owned by `EtmV4StreamDemultiplexer`.
//   3. De-multiplexed ETM data is sent to its corresponding `EtmV4Stream` where
//      it is stored in `TraceStorage`.
//
// Data flow for raw data (TRBE):
//   1. `PerfDataTokenizer` parses `AuxData` for cpu x and forwards it to the
//      `AuxDataStream` bound to that cpu
//   2. `EtmV4Stream` bound to cpu x determines AuxData is raw and can directly
//      store it in `TraceStorage`.
class EtmV4StreamDemultiplexer : public perf_importer::AuxDataTokenizer {
 public:
  explicit EtmV4StreamDemultiplexer(TraceProcessorContext* context,
                                    EtmTracker* etm_tracker)
      : context_(context), etm_tracker_(etm_tracker) {}
  ~EtmV4StreamDemultiplexer() override = default;

  base::StatusOr<perf_importer::AuxDataStream*> InitializeAuxDataStream(
      perf_importer::AuxStream* stream) override {
    if (stream->type() != perf_importer::AuxStream::Type::kCpuBound) {
      return base::ErrStatus("ETM only supports CPU bound AUX streams");
    }

    auto it = streams_.Find(stream->cpu());
    if (!it) {
      return base::ErrStatus("No EtmV4Stream for CPU: %" PRIu32, stream->cpu());
    }

    return it->get();
  }

  base::Status Init(perf_importer::AuxtraceInfoRecord info) {
    RETURN_IF_ERROR(decoder_.Init());
    ASSIGN_OR_RETURN(PerCpuConfiguration per_cpu_configuration,
                     ParseAuxtraceInfo(std::move(info)));

    for (auto id :
         etm_tracker_->InsertEtmV4Config(std::move(per_cpu_configuration))) {
      RETURN_IF_ERROR(InitCpu(id));
    }
    return base::OkStatus();
  }

 private:
  base::Status InitCpu(tables::EtmV4ConfigurationTable::Id config_id) {
    auto config =
        *context_->storage->etm_v4_configuration_table().FindById(config_id);

    auto stream = std::make_unique<EtmV4Stream>(context_, &decoder_, config_id);

    RETURN_IF_ERROR(decoder_.Attach(
        static_cast<uint8_t>(config.cs_trace_stream_id()), stream.get()));
    PERFETTO_CHECK(streams_.Insert(config.cpu(), std::move(stream)).second);
    return base::OkStatus();
  }

  TraceProcessorContext* const context_;
  EtmTracker* const etm_tracker_;

  FrameDecoder decoder_;
  base::FlatHashMap<uint32_t, std::unique_ptr<EtmV4Stream>> streams_;
};

}  // namespace

// static
base::StatusOr<std::unique_ptr<perf_importer::AuxDataTokenizer>>
CreateEtmV4StreamDemultiplexer(TraceProcessorContext* context,
                               EtmTracker* etm_tracker,
                               perf_importer::AuxtraceInfoRecord info) {
  std::unique_ptr<EtmV4StreamDemultiplexer> tokenizer(
      new EtmV4StreamDemultiplexer(context, etm_tracker));

  RETURN_IF_ERROR(tokenizer->Init(std::move(info)));

  return std::unique_ptr<perf_importer::AuxDataTokenizer>(std::move(tokenizer));
}

}  // namespace perfetto::trace_processor::etm
