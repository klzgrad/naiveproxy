// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/http2/hpack/decoder/hpack_decoder_state.h"

#include <utility>

#include "quiche/http2/http2_constants.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace http2 {
namespace {

std::string ExtractString(HpackDecoderStringBuffer* string_buffer) {
  if (string_buffer->IsBuffered()) {
    return string_buffer->ReleaseString();
  } else {
    auto result = std::string(string_buffer->str());
    string_buffer->Reset();
    return result;
  }
}

}  // namespace

HpackDecoderState::HpackDecoderState(HpackDecoderListener* listener)
    : listener_(listener),
      final_header_table_size_(Http2SettingsInfo::DefaultHeaderTableSize()),
      lowest_header_table_size_(final_header_table_size_),
      require_dynamic_table_size_update_(false),
      allow_dynamic_table_size_update_(true),
      saw_dynamic_table_size_update_(false),
      error_(HpackDecodingError::kOk) {
  QUICHE_CHECK(listener_);
}

HpackDecoderState::~HpackDecoderState() = default;

void HpackDecoderState::ApplyHeaderTableSizeSetting(
    uint32_t header_table_size) {
  QUICHE_DVLOG(2) << "HpackDecoderState::ApplyHeaderTableSizeSetting("
                  << header_table_size << ")";
  QUICHE_DCHECK_LE(lowest_header_table_size_, final_header_table_size_);
  if (header_table_size < lowest_header_table_size_) {
    lowest_header_table_size_ = header_table_size;
  }
  final_header_table_size_ = header_table_size;
  QUICHE_DVLOG(2) << "low water mark: " << lowest_header_table_size_;
  QUICHE_DVLOG(2) << "final limit: " << final_header_table_size_;
}

// Called to notify this object that we're starting to decode an HPACK block
// (e.g. a HEADERS or PUSH_PROMISE frame's header has been decoded).
void HpackDecoderState::OnHeaderBlockStart() {
  QUICHE_DVLOG(2) << "HpackDecoderState::OnHeaderBlockStart";
  // This instance can't be reused after an error has been detected, as we must
  // assume that the encoder and decoder compression states are no longer
  // synchronized.
  QUICHE_DCHECK(error_ == HpackDecodingError::kOk)
      << HpackDecodingErrorToString(error_);
  QUICHE_DCHECK_LE(lowest_header_table_size_, final_header_table_size_);
  allow_dynamic_table_size_update_ = true;
  saw_dynamic_table_size_update_ = false;
  // If the peer has acknowledged a HEADER_TABLE_SIZE smaller than that which
  // its HPACK encoder has been using, then the next HPACK block it sends MUST
  // start with a Dynamic Table Size Update entry that is at least as low as
  // lowest_header_table_size_. That may be followed by another as great as
  // final_header_table_size_, if those are different.
  require_dynamic_table_size_update_ =
      (lowest_header_table_size_ <
           decoder_tables_.current_header_table_size() ||
       final_header_table_size_ < decoder_tables_.header_table_size_limit());
  QUICHE_DVLOG(2) << "HpackDecoderState::OnHeaderListStart "
                  << "require_dynamic_table_size_update_="
                  << require_dynamic_table_size_update_;
  listener_->OnHeaderListStart();
}

void HpackDecoderState::OnIndexedHeader(size_t index) {
  QUICHE_DVLOG(2) << "HpackDecoderState::OnIndexedHeader: " << index;
  if (error_ != HpackDecodingError::kOk) {
    return;
  }
  if (require_dynamic_table_size_update_) {
    ReportError(HpackDecodingError::kMissingDynamicTableSizeUpdate, "");
    return;
  }
  allow_dynamic_table_size_update_ = false;
  const HpackStringPair* entry = decoder_tables_.Lookup(index);
  if (entry != nullptr) {
    listener_->OnHeader(entry->name, entry->value);
  } else {
    ReportError(HpackDecodingError::kInvalidIndex, "");
  }
}

void HpackDecoderState::OnNameIndexAndLiteralValue(
    HpackEntryType entry_type, size_t name_index,
    HpackDecoderStringBuffer* value_buffer) {
  QUICHE_DVLOG(2) << "HpackDecoderState::OnNameIndexAndLiteralValue "
                  << entry_type << ", " << name_index << ", "
                  << value_buffer->str();
  if (error_ != HpackDecodingError::kOk) {
    return;
  }
  if (require_dynamic_table_size_update_) {
    ReportError(HpackDecodingError::kMissingDynamicTableSizeUpdate, "");
    return;
  }
  allow_dynamic_table_size_update_ = false;
  const HpackStringPair* entry = decoder_tables_.Lookup(name_index);
  if (entry != nullptr) {
    std::string value(ExtractString(value_buffer));
    listener_->OnHeader(entry->name, value);
    if (entry_type == HpackEntryType::kIndexedLiteralHeader) {
      decoder_tables_.Insert(entry->name, std::move(value));
    }
  } else {
    ReportError(HpackDecodingError::kInvalidNameIndex, "");
  }
}

void HpackDecoderState::OnLiteralNameAndValue(
    HpackEntryType entry_type, HpackDecoderStringBuffer* name_buffer,
    HpackDecoderStringBuffer* value_buffer) {
  QUICHE_DVLOG(2) << "HpackDecoderState::OnLiteralNameAndValue " << entry_type
                  << ", " << name_buffer->str() << ", " << value_buffer->str();
  if (error_ != HpackDecodingError::kOk) {
    return;
  }
  if (require_dynamic_table_size_update_) {
    ReportError(HpackDecodingError::kMissingDynamicTableSizeUpdate, "");
    return;
  }
  allow_dynamic_table_size_update_ = false;
  std::string name(ExtractString(name_buffer));
  std::string value(ExtractString(value_buffer));
  listener_->OnHeader(name, value);
  if (entry_type == HpackEntryType::kIndexedLiteralHeader) {
    decoder_tables_.Insert(std::move(name), std::move(value));
  }
}

void HpackDecoderState::OnDynamicTableSizeUpdate(size_t size_limit) {
  QUICHE_DVLOG(2) << "HpackDecoderState::OnDynamicTableSizeUpdate "
                  << size_limit << ", required="
                  << (require_dynamic_table_size_update_ ? "true" : "false")
                  << ", allowed="
                  << (allow_dynamic_table_size_update_ ? "true" : "false");
  if (error_ != HpackDecodingError::kOk) {
    return;
  }
  QUICHE_DCHECK_LE(lowest_header_table_size_, final_header_table_size_);
  if (!allow_dynamic_table_size_update_) {
    // At most two dynamic table size updates allowed at the start, and not
    // after a header.
    ReportError(HpackDecodingError::kDynamicTableSizeUpdateNotAllowed, "");
    return;
  }
  if (require_dynamic_table_size_update_) {
    // The new size must not be greater than the low water mark.
    if (size_limit > lowest_header_table_size_) {
      ReportError(
          HpackDecodingError::kInitialDynamicTableSizeUpdateIsAboveLowWaterMark,
          "");
      return;
    }
    require_dynamic_table_size_update_ = false;
  } else if (size_limit > final_header_table_size_) {
    // The new size must not be greater than the final max header table size
    // that the peer acknowledged.
    ReportError(
        HpackDecodingError::kDynamicTableSizeUpdateIsAboveAcknowledgedSetting,
        "");
    return;
  }
  decoder_tables_.DynamicTableSizeUpdate(size_limit);
  if (saw_dynamic_table_size_update_) {
    allow_dynamic_table_size_update_ = false;
  } else {
    saw_dynamic_table_size_update_ = true;
  }
  // We no longer need to keep an eye out for a lower header table size.
  lowest_header_table_size_ = final_header_table_size_;
}

void HpackDecoderState::OnHpackDecodeError(HpackDecodingError error,
                                           std::string detailed_error) {
  QUICHE_DVLOG(2) << "HpackDecoderState::OnHpackDecodeError "
                  << HpackDecodingErrorToString(error);
  if (error_ == HpackDecodingError::kOk) {
    ReportError(error, detailed_error);
  }
}

void HpackDecoderState::OnHeaderBlockEnd() {
  QUICHE_DVLOG(2) << "HpackDecoderState::OnHeaderBlockEnd";
  if (error_ != HpackDecodingError::kOk) {
    return;
  }
  if (require_dynamic_table_size_update_) {
    // Apparently the HPACK block was empty, but we needed it to contain at
    // least 1 dynamic table size update.
    ReportError(HpackDecodingError::kMissingDynamicTableSizeUpdate, "");
  } else {
    listener_->OnHeaderListEnd();
  }
}

void HpackDecoderState::ReportError(HpackDecodingError error,
                                    std::string detailed_error) {
  QUICHE_DVLOG(2) << "HpackDecoderState::ReportError is new="
                  << (error_ == HpackDecodingError::kOk ? "true" : "false")
                  << ", error: " << HpackDecodingErrorToString(error);
  if (error_ == HpackDecodingError::kOk) {
    listener_->OnHeaderErrorDetected(HpackDecodingErrorToString(error));
    error_ = error;
    detailed_error_ = detailed_error;
  }
}

}  // namespace http2
