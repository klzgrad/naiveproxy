// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/spdy/core/hpack/hpack_decoder_adapter.h"

#include "base/logging.h"
#include "net/third_party/http2/decoder/decode_buffer.h"
#include "net/third_party/http2/decoder/decode_status.h"
#include "net/third_party/spdy/platform/api/spdy_estimate_memory_usage.h"

using ::http2::DecodeBuffer;
using ::http2::HpackEntryType;
using ::http2::HpackString;

namespace spdy {
namespace {
const size_t kMaxDecodeBufferSizeBytes = 32 * 1024;  // 32 KB
}  // namespace

HpackDecoderAdapter::HpackDecoderAdapter()
    : hpack_decoder_(&listener_adapter_, kMaxDecodeBufferSizeBytes),
      max_decode_buffer_size_bytes_(kMaxDecodeBufferSizeBytes),
      header_block_started_(false) {}

HpackDecoderAdapter::~HpackDecoderAdapter() = default;

void HpackDecoderAdapter::ApplyHeaderTableSizeSetting(size_t size_setting) {
  DVLOG(2) << "HpackDecoderAdapter::ApplyHeaderTableSizeSetting";
  hpack_decoder_.ApplyHeaderTableSizeSetting(size_setting);
}

void HpackDecoderAdapter::HandleControlFrameHeadersStart(
    SpdyHeadersHandlerInterface* handler) {
  DVLOG(2) << "HpackDecoderAdapter::HandleControlFrameHeadersStart";
  DCHECK(!header_block_started_);
  listener_adapter_.set_handler(handler);
}

bool HpackDecoderAdapter::HandleControlFrameHeadersData(
    const char* headers_data,
    size_t headers_data_length) {
  DVLOG(2) << "HpackDecoderAdapter::HandleControlFrameHeadersData: len="
           << headers_data_length;
  if (!header_block_started_) {
    // Initialize the decoding process here rather than in
    // HandleControlFrameHeadersStart because that method is not always called.
    header_block_started_ = true;
    if (!hpack_decoder_.StartDecodingBlock()) {
      header_block_started_ = false;
      return false;
    }
  }

  // Sometimes we get a call with headers_data==nullptr and
  // headers_data_length==0, in which case we need to avoid creating
  // a DecodeBuffer, which would otherwise complain.
  if (headers_data_length > 0) {
    DCHECK_NE(headers_data, nullptr);
    if (headers_data_length > max_decode_buffer_size_bytes_) {
      DVLOG(1) << "max_decode_buffer_size_bytes_ < headers_data_length: "
               << max_decode_buffer_size_bytes_ << " < " << headers_data_length;
      return false;
    }
    listener_adapter_.AddToTotalHpackBytes(headers_data_length);
    http2::DecodeBuffer db(headers_data, headers_data_length);
    bool ok = hpack_decoder_.DecodeFragment(&db);
    DCHECK(!ok || db.Empty()) << "Remaining=" << db.Remaining();
    return ok;
  }
  return true;
}

bool HpackDecoderAdapter::HandleControlFrameHeadersComplete(
    size_t* compressed_len) {
  DVLOG(2) << "HpackDecoderAdapter::HandleControlFrameHeadersComplete";
  if (compressed_len != nullptr) {
    *compressed_len = listener_adapter_.total_hpack_bytes();
  }
  if (!hpack_decoder_.EndDecodingBlock()) {
    DVLOG(3) << "EndDecodingBlock returned false";
    return false;
  }
  header_block_started_ = false;
  return true;
}

const SpdyHeaderBlock& HpackDecoderAdapter::decoded_block() const {
  return listener_adapter_.decoded_block();
}

void HpackDecoderAdapter::SetHeaderTableDebugVisitor(
    std::unique_ptr<HpackHeaderTable::DebugVisitorInterface> visitor) {
  DVLOG(2) << "HpackDecoderAdapter::SetHeaderTableDebugVisitor";
  if (visitor != nullptr) {
    listener_adapter_.SetHeaderTableDebugVisitor(std::move(visitor));
    hpack_decoder_.set_tables_debug_listener(&listener_adapter_);
  } else {
    hpack_decoder_.set_tables_debug_listener(nullptr);
    listener_adapter_.SetHeaderTableDebugVisitor(nullptr);
  }
}

void HpackDecoderAdapter::set_max_decode_buffer_size_bytes(
    size_t max_decode_buffer_size_bytes) {
  DVLOG(2) << "HpackDecoderAdapter::set_max_decode_buffer_size_bytes";
  max_decode_buffer_size_bytes_ = max_decode_buffer_size_bytes;
  hpack_decoder_.set_max_string_size_bytes(max_decode_buffer_size_bytes);
}

size_t HpackDecoderAdapter::EstimateMemoryUsage() const {
  return SpdyEstimateMemoryUsage(hpack_decoder_);
}

HpackDecoderAdapter::ListenerAdapter::ListenerAdapter() : handler_(nullptr) {}
HpackDecoderAdapter::ListenerAdapter::~ListenerAdapter() = default;

void HpackDecoderAdapter::ListenerAdapter::set_handler(
    SpdyHeadersHandlerInterface* handler) {
  handler_ = handler;
}

void HpackDecoderAdapter::ListenerAdapter::SetHeaderTableDebugVisitor(
    std::unique_ptr<HpackHeaderTable::DebugVisitorInterface> visitor) {
  visitor_ = std::move(visitor);
}

void HpackDecoderAdapter::ListenerAdapter::OnHeaderListStart() {
  DVLOG(2) << "HpackDecoderAdapter::ListenerAdapter::OnHeaderListStart";
  total_hpack_bytes_ = 0;
  total_uncompressed_bytes_ = 0;
  decoded_block_.clear();
  if (handler_ != nullptr) {
    handler_->OnHeaderBlockStart();
  }
}

void HpackDecoderAdapter::ListenerAdapter::OnHeader(HpackEntryType entry_type,
                                                    const HpackString& name,
                                                    const HpackString& value) {
  DVLOG(2) << "HpackDecoderAdapter::ListenerAdapter::OnHeader:\n name: " << name
           << "\n value: " << value;
  total_uncompressed_bytes_ += name.size() + value.size();
  if (handler_ == nullptr) {
    DVLOG(3) << "Adding to decoded_block";
    decoded_block_.AppendValueOrAddHeader(name.ToStringPiece(),
                                          value.ToStringPiece());
  } else {
    DVLOG(3) << "Passing to handler";
    handler_->OnHeader(name.ToStringPiece(), value.ToStringPiece());
  }
}

void HpackDecoderAdapter::ListenerAdapter::OnHeaderListEnd() {
  DVLOG(2) << "HpackDecoderAdapter::ListenerAdapter::OnHeaderListEnd";
  // We don't clear the SpdyHeaderBlock here to allow access to it until the
  // next HPACK block is decoded.
  if (handler_ != nullptr) {
    handler_->OnHeaderBlockEnd(total_uncompressed_bytes_, total_hpack_bytes_);
    handler_ = nullptr;
  }
}

void HpackDecoderAdapter::ListenerAdapter::OnHeaderErrorDetected(
    SpdyStringPiece error_message) {
  VLOG(1) << error_message;
}

int64_t HpackDecoderAdapter::ListenerAdapter::OnEntryInserted(
    const http2::HpackStringPair& sp,
    size_t insert_count) {
  DVLOG(2) << "HpackDecoderAdapter::ListenerAdapter::OnEntryInserted: " << sp
           << ",  insert_count=" << insert_count;
  if (visitor_ == nullptr) {
    return 0;
  }
  HpackEntry entry(sp.name.ToStringPiece(), sp.value.ToStringPiece(),
                   /*is_static*/ false, insert_count);
  int64_t time_added = visitor_->OnNewEntry(entry);
  DVLOG(2)
      << "HpackDecoderAdapter::ListenerAdapter::OnEntryInserted: time_added="
      << time_added;
  return time_added;
}

void HpackDecoderAdapter::ListenerAdapter::OnUseEntry(
    const http2::HpackStringPair& sp,
    size_t insert_count,
    int64_t time_added) {
  DVLOG(2) << "HpackDecoderAdapter::ListenerAdapter::OnUseEntry: " << sp
           << ",  insert_count=" << insert_count
           << ",  time_added=" << time_added;
  if (visitor_ != nullptr) {
    HpackEntry entry(sp.name.ToStringPiece(), sp.value.ToStringPiece(),
                     /*is_static*/ false, insert_count);
    entry.set_time_added(time_added);
    visitor_->OnUseEntry(entry);
  }
}

}  // namespace spdy
