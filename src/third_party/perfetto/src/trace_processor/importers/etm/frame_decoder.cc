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

#include "src/trace_processor/importers/etm/frame_decoder.h"

#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"

namespace perfetto::trace_processor::etm {

base::Status FrameDecoder::Init() {
  RETURN_IF_ERROR(error_logger_.ToStatus(frame_decoder_.Init()));
  PERFETTO_CHECK(frame_decoder_.getErrLogAttachPt()->attach(&error_logger_) ==
                 OCSD_OK);
  RETURN_IF_ERROR(error_logger_.ToStatus(
      frame_decoder_.Configure(OCSD_DFRMTR_FRAME_MEM_ALIGN)));
  frame_decoder_.SetDemuxStatsBlock(&demux_stats_);
  return base::OkStatus();
}

base::StatusOr<bool> FrameDecoder::TraceDataIn(const ocsd_datapath_op_t op,
                                               const ocsd_trc_index_t index,
                                               const uint32_t data_block_size,
                                               const uint8_t* data_block,
                                               uint32_t* num_bytes_processed) {
  auto resp = frame_decoder_.TraceDataIn(op, index, data_block_size, data_block,
                                         num_bytes_processed);
  return error_logger_.ToErrorOrKeepGoing(resp);
}

base::Status FrameDecoder::Attach(uint8_t cs_trace_stream_id,
                                  ITrcDataIn* data_in) {
  return error_logger_.ToStatus(
      frame_decoder_.getIDStreamAttachPt(cs_trace_stream_id)->attach(data_in));
}

}  // namespace perfetto::trace_processor::etm
