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

#include "src/trace_processor/importers/perf/perf_tracker.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "src/trace_processor/importers/common/address_range.h"
#include "src/trace_processor/importers/common/create_mapping_params.h"
#include "src/trace_processor/importers/common/mapping_tracker.h"
#include "src/trace_processor/importers/common/registered_file_tracker.h"
#include "src/trace_processor/importers/common/symbol_tracker.h"
#include "src/trace_processor/importers/common/virtual_memory_mapping.h"
#include "src/trace_processor/importers/perf/aux_data_tokenizer.h"
#include "src/trace_processor/importers/perf/auxtrace_info_record.h"
#include "src/trace_processor/importers/perf/perf_event.h"
#include "src/trace_processor/importers/perf/spe_tokenizer.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/perf_tables_py.h"
#include "src/trace_processor/tables/profiler_tables_py.h"

#include "protos/third_party/simpleperf/record_file.pbzero.h"

#if PERFETTO_BUILDFLAG(PERFETTO_ENABLE_ETM_IMPORTER)
#include "src/trace_processor/importers/etm/etm_tracker.h"
#include "src/trace_processor/importers/etm/etm_v4_stream_demultiplexer.h"
#endif

namespace perfetto::trace_processor::perf_importer {
namespace {

using third_party::simpleperf::proto::pbzero::FileFeature;
using DexFile = FileFeature::DexFile;
using ElfFile = FileFeature::ElfFile;
using KernelModule = FileFeature::KernelModule;
using DsoType = FileFeature::DsoType;
using Symbol = FileFeature::Symbol;

void InsertSymbols(const FileFeature::Decoder& file,
                   AddressRangeMap<std::string>& out) {
  for (auto raw_symbol = file.symbol(); raw_symbol; ++raw_symbol) {
    Symbol::Decoder symbol(*raw_symbol);
    out.TrimOverlapsAndEmplace(
        AddressRange::FromStartAndSize(symbol.vaddr(), symbol.len()),
        symbol.name().ToStdString());
  }
}

bool IsBpfMapping(const CreateMappingParams& params) {
  return params.name == "[bpf]";
}

}  // namespace

PerfTracker::PerfTracker(TraceProcessorContext* context) : context_(context) {
  RegisterAuxTokenizer(PERF_AUXTRACE_ARM_SPE, &SpeTokenizer::Create);
#if PERFETTO_BUILDFLAG(PERFETTO_ENABLE_ETM_IMPORTER)
  etm_tracker_ = std::make_unique<etm::EtmTracker>(context);
  RegisterAuxTokenizer(PERF_AUXTRACE_CS_ETM,
                       etm::CreateEtmV4StreamDemultiplexer);
#endif
}

base::StatusOr<std::unique_ptr<AuxDataTokenizer>>
PerfTracker::CreateAuxDataTokenizer(AuxtraceInfoRecord info) {
  auto* it = factories_.Find(info.type);
  if (!it) {
    return std::unique_ptr<AuxDataTokenizer>(
        new DummyAuxDataTokenizer(context_));
  }
  etm::EtmTracker* etm_tracker = nullptr;
#if PERFETTO_BUILDFLAG(PERFETTO_ENABLE_ETM_IMPORTER)
  etm_tracker = static_cast<etm::EtmTracker*>(etm_tracker_.get());
#endif
  return (*it)(context_, etm_tracker, std::move(info));
}

void PerfTracker::AddSimpleperfFile2(const FileFeature::Decoder& file) {
  SymbolTracker::Dso dso;
  switch (file.type()) {
    case DsoType::DSO_KERNEL: {
      InsertSymbols(file, context_->symbol_tracker->kernel_symbols());
      return;
    }
    case DsoType::DSO_ELF_FILE: {
      ElfFile::Decoder elf(file.elf_file());
      dso.load_bias = file.min_vaddr() - elf.file_offset_of_min_vaddr();
      break;
    }
    case DsoType::DSO_KERNEL_MODULE: {
      KernelModule::Decoder module(file.kernel_module());
      dso.load_bias = file.min_vaddr() - module.memory_offset_of_min_vaddr();
      break;
    }
    case DsoType::DSO_DEX_FILE: {
      break;
    }
    case DsoType::DSO_SYMBOL_MAP_FILE:
    case DsoType::DSO_UNKNOWN_FILE:
    default:
      return;
  }

  // JIT functions use absolute addresses for their symbols instead of relative
  // ones.
  std::string path = file.path().ToStdString();
  dso.symbols_are_absolute = base::Contains(path, "jit_app_cache") ||
                             base::Contains(path, "jit_zygote_cache");
  InsertSymbols(file, dso.symbols);
  context_->symbol_tracker->dsos().Insert(
      context_->storage->InternString(file.path()), std::move(dso));
}

void PerfTracker::CreateKernelMemoryMapping(int64_t trace_ts,
                                            CreateMappingParams params) {
  // Ignore BPF mapping that spans the entire memory range
  if (IsBpfMapping(params) &&
      params.memory_range.size() == std::numeric_limits<uint64_t>::max()) {
    return;
  }

  // Linux perf synthesises special MMAP/MMAP2 records for the kernel image.
  // In particular, the KASLR address of _text is stored in the `pgoff` field.
  // This needs special treatment since the kernel ELF is not in fact
  // 0xffffff... in size. See:
  // * https://elixir.bootlin.com/linux/v6.16/source/tools/perf/util/synthetic-events.c#L1156
  // * https://lore.kernel.org/lkml/20201214105457.543111-1-jolsa@kernel.org
  //
  // TODO(lalitm): we are not correctly handling guest kernels, add support for
  // that once we have some real traces with those present.
  if (base::StartsWith(params.name, "[kernel.kallsyms]")) {
    params.exact_offset = 0;
  }
  AddMapping(
      trace_ts, std::nullopt,
      context_->mapping_tracker->CreateKernelMemoryMapping(std::move(params)));
}

void PerfTracker::CreateUserMemoryMapping(int64_t trace_ts,
                                          UniquePid upid,
                                          CreateMappingParams params) {
  AddMapping(trace_ts, upid,
             context_->mapping_tracker->CreateUserMemoryMapping(
                 upid, std::move(params)));
}

void PerfTracker::AddMapping(int64_t trace_ts,
                             std::optional<UniquePid> upid,
                             const VirtualMemoryMapping& mapping) {
  tables::MmapRecordTable::Row row;
  row.ts = trace_ts;
  row.upid = upid;
  row.mapping_id = mapping.mapping_id();
  if (mapping.build_id()) {
    if (auto id =
            context_->registered_file_tracker->FindBuildId(*mapping.build_id());
        id) {
      row.file_id =
          context_->storage->elf_file_table().FindById(*id)->file_id();
    }
  }
  context_->storage->mutable_mmap_record_table()->Insert(row);
}

base::Status PerfTracker::NotifyEndOfFile() {
#if PERFETTO_BUILDFLAG(PERFETTO_ENABLE_ETM_IMPORTER)
  RETURN_IF_ERROR(
      static_cast<etm::EtmTracker*>(etm_tracker_.get())->Finalize());
#endif
  return base::OkStatus();
}

void PerfTracker::RegisterAuxTokenizer(uint32_t type,
                                       AuxDataTokenizerFactory factory) {
  PERFETTO_CHECK(factories_.Insert(type, std::move(factory)).second);
}

}  // namespace perfetto::trace_processor::perf_importer
