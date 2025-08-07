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

#include "src/trace_processor/importers/perf/spe_record_parser.h"

#include <cstddef>
#include <cstdint>
#include <optional>

#include "perfetto/base/logging.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/importers/common/mapping_tracker.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/virtual_memory_mapping.h"
#include "src/trace_processor/importers/perf/reader.h"
#include "src/trace_processor/importers/perf/spe.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/metadata_tables_py.h"
#include "src/trace_processor/tables/perf_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor::perf_importer {

// static
const char* SpeRecordParserImpl::ToString(spe::DataSource ds) {
  switch (ds) {
    case spe::DataSource::kUnknown:
      return "UNKNOWN";
    case spe::DataSource::kL1D:
      return "L1D";
    case spe::DataSource::kL2:
      return "L2";
    case spe::DataSource::kPeerCore:
      return "PEER_CORE";
    case spe::DataSource::kLocalCluster:
      return "LOCAL_CLUSTER";
    case spe::DataSource::kSysCache:
      return "SYS_CACHE";
    case spe::DataSource::kPeerCluster:
      return "PEER_CLUSTER";
    case spe::DataSource::kRemote:
      return "REMOTE";
    case spe::DataSource::kDram:
      return "DRAM";
  }
  PERFETTO_FATAL("For GCC");
}

// static
const char* SpeRecordParserImpl::ToString(spe::ExceptionLevel el) {
  switch (el) {
    case spe::ExceptionLevel::kEl0:
      return "EL0";
    case spe::ExceptionLevel::kEl1:
      return "EL1";
    case spe::ExceptionLevel::kEl2:
      return "EL2";
    case spe::ExceptionLevel::kEl3:
      return "EL3";
  }
  PERFETTO_FATAL("For GCC");
}

// static
const char* SpeRecordParserImpl::ToString(OperationName name) {
  switch (name) {
    case OperationName::kOther:
      return "OTHER";
    case OperationName::kSveVecOp:
      return "SVE_VEC_OP";
    case OperationName::kLoad:
      return "LOAD";
    case OperationName::kStore:
      return "STORE";
    case OperationName::kBranch:
      return "BRANCH";
    case OperationName::kUnknown:
      return "UNKNOWN";
  }
  PERFETTO_FATAL("For GCC");
}

StringId SpeRecordParserImpl::ToStringId(OperationName name) {
  if (operation_name_strings_[name] == kNullStringId) {
    operation_name_strings_[name] =
        context_->storage->InternString(ToString(name));
  }
  return operation_name_strings_[name];
}

StringId SpeRecordParserImpl::ToStringId(spe::ExceptionLevel el) {
  if (exception_level_strings_[el] == kNullStringId) {
    exception_level_strings_[el] =
        context_->storage->InternString(ToString(el));
  }
  return exception_level_strings_[el];
}

StringId SpeRecordParserImpl::ToStringId(spe::DataSource ds) {
  if (data_source_strings_[ds] == kNullStringId) {
    data_source_strings_[ds] = context_->storage->InternString(ToString(ds));
  }
  return data_source_strings_[ds];
}

SpeRecordParserImpl::SpeRecordParserImpl(TraceProcessorContext* context)
    : context_(context), reader_(TraceBlobView()) {}

void SpeRecordParserImpl::ParseSpeRecord(int64_t ts, TraceBlobView data) {
  reader_ = Reader(std::move(data));
  inflight_row_ = {};
  inflight_row_.ts = ts;
  inflight_record_ = {};

  // No need to check that there is enough data as this has been validated by
  // the tokenization step.
  while (reader_.size_left() != 0) {
    uint8_t byte_0;
    reader_.Read(byte_0);

    if (spe::IsExtendedHeader(byte_0)) {
      uint8_t byte_1;
      reader_.Read(byte_1);
      spe::ExtendedHeader extended_header(byte_0, byte_1);
      ReadExtendedPacket(extended_header);
    } else {
      ReadShortPacket(spe::ShortHeader(byte_0));
    }
  }
  if (!inflight_record_.instruction_address) {
    context_->storage->mutable_spe_record_table()->Insert(inflight_row_);
    return;
  }

  const auto& inst = *inflight_record_.instruction_address;

  inflight_row_.exception_level = ToStringId(inst.el);

  if (inst.el == spe::ExceptionLevel::kEl0 && inflight_row_.utid) {
    const auto upid =
        *context_->storage->thread_table()
             .FindById(tables::ThreadTable::Id(*inflight_row_.utid))
             ->upid();

    VirtualMemoryMapping* mapping =
        context_->mapping_tracker->FindUserMappingForAddress(upid,
                                                             inst.address);
    if (mapping) {
      inflight_row_.instruction_frame_id =
          mapping->InternFrame(mapping->ToRelativePc(inst.address), "");
    }
  } else if (inst.el == spe::ExceptionLevel::kEl1) {
    VirtualMemoryMapping* mapping =
        context_->mapping_tracker->FindKernelMappingForAddress(inst.address);
    if (mapping) {
      inflight_row_.instruction_frame_id =
          mapping->InternFrame(mapping->ToRelativePc(inst.address), "");
    }
  }

  if (!inflight_row_.instruction_frame_id.has_value()) {
    inflight_row_.instruction_frame_id = GetDummyMapping()->InternFrame(
        GetDummyMapping()->ToRelativePc(inst.address), "");
  }

  context_->storage->mutable_spe_record_table()->Insert(inflight_row_);
}

void SpeRecordParserImpl::ReadShortPacket(spe::ShortHeader short_header) {
  if (short_header.IsAddressPacket()) {
    ReadAddressPacket(short_header.GetAddressIndex());

  } else if (short_header.IsCounterPacket()) {
    ReadCounterPacket(short_header.GetCounterIndex());

  } else if (short_header.IsEventsPacket()) {
    ReadEventsPacket(short_header);

  } else if (short_header.IsContextPacket()) {
    ReadContextPacket(short_header);

  } else if (short_header.IsOperationTypePacket()) {
    ReadOperationTypePacket(short_header);

  } else if (short_header.IsDataSourcePacket()) {
    ReadDataSourcePacket(short_header);

  } else {
    reader_.Skip(short_header.GetPayloadSize());
  }
}

void SpeRecordParserImpl::ReadExtendedPacket(
    spe::ExtendedHeader extended_header) {
  if (extended_header.IsAddressPacket()) {
    ReadAddressPacket(extended_header.GetAddressIndex());

  } else if (extended_header.IsCounterPacket()) {
    ReadCounterPacket(extended_header.GetCounterIndex());

  } else {
    reader_.Skip(extended_header.GetPayloadSize());
  }
}

void SpeRecordParserImpl::ReadAddressPacket(spe::AddressIndex index) {
  uint64_t payload;
  reader_.Read(payload);

  switch (index) {
    case spe::AddressIndex::kInstruction:
      inflight_record_.instruction_address =
          spe::InstructionVirtualAddress(payload);
      break;

    case spe::AddressIndex::kDataVirtual:
      inflight_row_.data_virtual_address =
          static_cast<int64_t>(spe::DataVirtualAddress(payload).address);
      break;

    case spe::AddressIndex::kDataPhysical:
      inflight_row_.data_physical_address =
          static_cast<int64_t>(spe::DataPhysicalAddress(payload).address);
      break;

    case spe::AddressIndex::kBranchTarget:
    case spe::AddressIndex::kPrevBranchTarget:
    case spe::AddressIndex::kUnknown:
      break;
  }
}

void SpeRecordParserImpl::ReadCounterPacket(spe::CounterIndex index) {
  uint16_t value;
  reader_.Read(value);
  switch (index) {
    case spe::CounterIndex::kTotalLatency:
      inflight_row_.total_latency = value;
      break;

    case spe::CounterIndex::kIssueLatency:
      inflight_row_.issue_latency = value;
      break;

    case spe::CounterIndex::kTranslationLatency:
      inflight_row_.translation_latency = value;
      break;

    case spe::CounterIndex::kUnknown:
      break;
  }
}

void SpeRecordParserImpl::ReadEventsPacket(spe::ShortHeader short_header) {
  inflight_row_.events_bitmask =
      static_cast<int64_t>(ReadPayload(short_header));
}

void SpeRecordParserImpl::ReadContextPacket(spe::ShortHeader short_header) {
  uint32_t tid;
  reader_.Read(tid);
  inflight_row_.utid = context_->process_tracker->GetOrCreateThread(tid);
  switch (short_header.GetContextIndex()) {
    case spe::ContextIndex::kEl1:
    case spe::ContextIndex::kEl2:
    case spe::ContextIndex::kUnknown:
      break;
  }
}

void SpeRecordParserImpl::ReadOperationTypePacket(
    spe::ShortHeader short_header) {
  uint8_t payload;
  reader_.Read(payload);
  inflight_row_.operation = ToStringId(GetOperationName(short_header, payload));
}

SpeRecordParserImpl::OperationName SpeRecordParserImpl::GetOperationName(
    spe::ShortHeader short_header,
    uint8_t payload) const {
  switch (short_header.GetOperationClass()) {
    case spe::OperationClass::kOther:
      switch (spe::OperationTypeOtherPayload(payload).subclass()) {
        case spe::OperationOtherSubclass::kOther:
          return OperationName::kOther;
        case spe::OperationOtherSubclass::kSveVecOp:
          return OperationName::kSveVecOp;
        case spe::OperationOtherSubclass::kUnknown:
          return OperationName::kUnknown;
      }
      PERFETTO_FATAL("For GCC");

    case spe::OperationClass::kLoadOrStoreOrAtomic:
      if (spe::OperationTypeLdStAtPayload(payload).IsStore()) {
        return OperationName::kStore;
      }
      return OperationName::kLoad;

    case spe::OperationClass::kBranchOrExceptionReturn:
      return OperationName::kBranch;

    case spe::OperationClass::kUnknown:
      return OperationName::kUnknown;
  }
  PERFETTO_FATAL("For GCC");
}

VirtualMemoryMapping* SpeRecordParserImpl::GetDummyMapping() {
  if (!dummy_mapping_) {
    dummy_mapping_ =
        &context_->mapping_tracker->CreateDummyMapping("spe_dummy");
  }
  return dummy_mapping_;
}

void SpeRecordParserImpl::ReadDataSourcePacket(spe::ShortHeader short_header) {
  inflight_row_.data_source =
      ToStringId(short_header.GetDataSource(ReadPayload(short_header)));
}

uint64_t SpeRecordParserImpl::ReadPayload(spe::ShortHeader short_header) {
  switch (short_header.GetPayloadSize()) {
    case 1: {
      uint8_t data;
      reader_.Read(data);
      return data;
    }
    case 2: {
      uint16_t data;
      reader_.Read(data);
      return data;
    }
    case 4: {
      uint32_t data;
      reader_.Read(data);
      return data;
    }
    case 8: {
      uint64_t data;
      reader_.Read(data);
      return data;
    }
    default:
      break;
  }
  PERFETTO_FATAL("Unreachable");
}

}  // namespace perfetto::trace_processor::perf_importer
