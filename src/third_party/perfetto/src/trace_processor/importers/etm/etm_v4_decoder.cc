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

#include "src/trace_processor/importers/etm/etm_v4_decoder.h"

#include <cstdint>
#include <limits>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "src/trace_processor/importers/etm/mapping_version.h"
#include "src/trace_processor/importers/etm/opencsd.h"
#include "src/trace_processor/importers/etm/target_memory_reader.h"

// Be aware the in the OSCD namespace an ETM chunk is an ETM trace.
namespace perfetto::trace_processor::etm {
namespace {
uint32_t ClampToUint32(size_t size) {
  if (size > std::numeric_limits<uint32_t>::max()) {
    return std::numeric_limits<uint32_t>::max();
  }
  return static_cast<uint32_t>(size);
}
}  // namespace

EtmV4Decoder::Delegate::~Delegate() = default;

// static
base::StatusOr<std::unique_ptr<EtmV4Decoder>> EtmV4Decoder::Create(
    Delegate* delegate,
    TargetMemoryReader* reader,
    const EtmV4Config& config) {
  std::unique_ptr<EtmV4Decoder> res(new EtmV4Decoder(delegate, reader));
  RETURN_IF_ERROR(res->Init(config));
  return std::move(res);
}

EtmV4Decoder::EtmV4Decoder(Delegate* delegate, TargetMemoryReader* reader)
    : delegate_(delegate), memory_reader_(reader) {}

base::Status EtmV4Decoder::Init(const EtmV4Config& config) {
  PERFETTO_CHECK(
      packet_decoder_.getErrorLogAttachPt()->attach(&error_logger_) == OCSD_OK);
  RETURN_IF_ERROR(
      error_logger_.ToStatus(packet_decoder_.setProtocolConfig(&config)));
  PERFETTO_CHECK(packet_decoder_.getInstrDecodeAttachPt()->attach(
                     &instruction_decoder_) == OCSD_OK);
  PERFETTO_CHECK(packet_decoder_.getMemoryAccessAttachPt()->attach(
                     memory_reader_) == OCSD_OK);
  PERFETTO_CHECK(packet_decoder_.getTraceElemOutAttachPt()->attach(this) ==
                 OCSD_OK);

  PERFETTO_CHECK(packet_processor_.getErrorLogAttachPt()->attach(
                     &error_logger_) == OCSD_OK);
  RETURN_IF_ERROR(
      error_logger_.ToStatus(packet_processor_.setProtocolConfig(&config)));
  PERFETTO_CHECK(packet_processor_.getPacketOutAttachPt()->attach(
                     &packet_decoder_) == OCSD_OK);

  return base::OkStatus();
}

ocsd_datapath_resp_t EtmV4Decoder::TraceElemIn(const ocsd_trc_index_t index_sop,
                                               const uint8_t trc_chan_id,
                                               const OcsdTraceElement& elem) {
  const MappingVersion* content = nullptr;
  if (elem.getType() == OCSD_GEN_TRC_ELEM_PE_CONTEXT) {
    memory_reader_->SetPeContext(elem.getContext());
  } else if (elem.getType() == OCSD_GEN_TRC_ELEM_INSTR_RANGE) {
    content = memory_reader_->FindMapping(elem.st_addr);
    PERFETTO_CHECK(content);
    if (!content->Contains(elem.en_addr)) {
      // Sometimes (very very rarely) we get huge instruction ranges that can
      // span multiple adjacent mappings caused by runaway decoding.
      // Some libraries get their code modified at load time (e.g. linux kernel
      // does some in place changes to code for high efficiency). When loading
      // loading the code for a file directly we do not have those modifications
      // and thus me might get into runaway decoding.
      PERFETTO_ELOG(
          "Mapping does not contain full instruction range. st_addr=%" PRIu64
          "en_addr=%" PRIu64,
          elem.st_addr, elem.en_addr);
    }
  } else if (elem.getType() == OCSD_GEN_TRC_ELEM_ADDR_NACC) {
    content = memory_reader_->FindMapping(elem.st_addr);
  }
  return delegate_->TraceElemIn(index_sop, trc_chan_id, elem, content);
}

base::StatusOr<bool> EtmV4Decoder::Reset(ocsd_trc_index_t index) {
  ocsd_datapath_resp_t resp =
      packet_processor_.TraceDataIn(OCSD_OP_RESET, index, 0, nullptr, nullptr);
  return error_logger_.ToErrorOrKeepGoing(resp);
}

base::StatusOr<bool> EtmV4Decoder::Flush(ocsd_trc_index_t index) {
  ocsd_datapath_resp_t resp =
      packet_processor_.TraceDataIn(OCSD_OP_FLUSH, index, 0, nullptr, nullptr);
  return error_logger_.ToErrorOrKeepGoing(resp);
}

base::StatusOr<bool> EtmV4Decoder::Data(const ocsd_trc_index_t index,
                                        const size_t size,
                                        const uint8_t* data,
                                        uint32_t* num_bytes_processed) {
  ocsd_datapath_resp_t resp = packet_processor_.TraceDataIn(
      OCSD_OP_DATA, index, ClampToUint32(size), data, num_bytes_processed);

  return error_logger_.ToErrorOrKeepGoing(resp);
}

base::StatusOr<bool> EtmV4Decoder::Eot(ocsd_trc_index_t index) {
  ocsd_datapath_resp_t resp =
      packet_processor_.TraceDataIn(OCSD_OP_EOT, index, 0, nullptr, nullptr);
  return error_logger_.ToErrorOrKeepGoing(resp);
}

}  // namespace perfetto::trace_processor::etm
