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
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/string_view.h"
#include "src/trace_processor/importers/common/address_range.h"
#include "src/trace_processor/importers/common/create_mapping_params.h"
#include "src/trace_processor/importers/common/mapping_tracker.h"
#include "src/trace_processor/importers/common/virtual_memory_mapping.h"
#include "src/trace_processor/importers/perf/aux_data_tokenizer.h"
#include "src/trace_processor/importers/perf/auxtrace_info_record.h"
#include "src/trace_processor/importers/perf/perf_event.h"
#include "src/trace_processor/importers/perf/spe_tokenizer.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/perf_tables_py.h"
#include "src/trace_processor/tables/profiler_tables_py.h"

#if PERFETTO_BUILDFLAG(PERFETTO_ENABLE_ETM_IMPORTER)
#include "src/trace_processor/importers/etm/elf_tracker.h"
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

PerfTracker::PerfTracker(TraceProcessorContext* context)
    : context_(context),
      mapping_table_(context->storage->stack_profile_mapping_table()) {
  RegisterAuxTokenizer(PERF_AUXTRACE_ARM_SPE, &SpeTokenizer::Create);
}

PerfTracker::~PerfTracker() = default;

base::StatusOr<std::unique_ptr<AuxDataTokenizer>>
PerfTracker::CreateAuxDataTokenizer(AuxtraceInfoRecord info) {
  auto* it = factories_.Find(info.type);
  if (!it) {
    return std::unique_ptr<AuxDataTokenizer>(
        new DummyAuxDataTokenizer(context_));
  }
  return (*it)(context_, std::move(info));
}

void PerfTracker::AddSimpleperfFile2(const FileFeature::Decoder& file) {
  Dso dso;
  switch (file.type()) {
    case DsoType::DSO_KERNEL:
      InsertSymbols(file, kernel_symbols_);
      return;

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

    case DsoType::DSO_DEX_FILE:
    case DsoType::DSO_SYMBOL_MAP_FILE:
    case DsoType::DSO_UNKNOWN_FILE:
    default:
      return;
  }

  InsertSymbols(file, dso.symbols);
  files_.Insert(context_->storage->InternString(file.path()), std::move(dso));
}

void PerfTracker::SymbolizeFrames() {
  const StringId kEmptyString = context_->storage->InternString("");
  for (auto frame = context_->storage->mutable_stack_profile_frame_table()
                        ->IterateRows();
       frame; ++frame) {
    if (frame.name() != kNullStringId && frame.name() != kEmptyString) {
      continue;
    }

    if (!TrySymbolizeFrame(frame.ToRowReference())) {
      SymbolizeKernelFrame(frame.ToRowReference());
    }
  }
}

void PerfTracker::SymbolizeKernelFrame(
    tables::StackProfileFrameTable::RowReference frame) {
  const auto mapping = *mapping_table_.FindById(frame.mapping());
  uint64_t address = static_cast<uint64_t>(frame.rel_pc()) +
                     static_cast<uint64_t>(mapping.start());
  auto symbol = kernel_symbols_.Find(address);
  if (symbol == kernel_symbols_.end()) {
    return;
  }
  frame.set_name(
      context_->storage->InternString(base::StringView(symbol->second)));
}

bool PerfTracker::TrySymbolizeFrame(
    tables::StackProfileFrameTable::RowReference frame) {
  const auto mapping = *mapping_table_.FindById(frame.mapping());
  auto* file = files_.Find(mapping.name());
  if (!file) {
    return false;
  }

  // Load bias is something we can only determine by looking at the actual elf
  // file. Thus PERF_RECORD_MMAP{2} events do not record it. So we need to
  // potentially do an adjustment here if the load_bias tracked in the mapping
  // table and the one reported by the file are mismatched.
  uint64_t adj = file->load_bias - static_cast<uint64_t>(mapping.load_bias());

  auto symbol = file->symbols.Find(static_cast<uint64_t>(frame.rel_pc()) + adj);
  if (symbol == file->symbols.end()) {
    return false;
  }
  frame.set_name(
      context_->storage->InternString(base::StringView(symbol->second)));
  return true;
}

void PerfTracker::CreateKernelMemoryMapping(int64_t trace_ts,
                                            CreateMappingParams params) {
  // Ignore BPF mapping that spans the entire memory range
  if (IsBpfMapping(params) &&
      params.memory_range.size() == std::numeric_limits<uint64_t>::max()) {
    return;
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
#if PERFETTO_BUILDFLAG(PERFETTO_ENABLE_ETM_IMPORTER)
  if (mapping.build_id()) {
    if (auto id = etm::ElfTracker::GetOrCreate(context_)->FindBuildId(
            *mapping.build_id());
        id) {
      row.file_id =
          context_->storage->elf_file_table().FindById(*id)->file_id();
    }
  }
#endif
  context_->storage->mutable_mmap_record_table()->Insert(row);
}

void PerfTracker::NotifyEndOfFile() {
  SymbolizeFrames();
}

void PerfTracker::RegisterAuxTokenizer(uint32_t type,
                                       AuxDataTokenizerFactory factory) {
  PERFETTO_CHECK(factories_.Insert(type, std::move(factory)).second);
}

}  // namespace perfetto::trace_processor::perf_importer
