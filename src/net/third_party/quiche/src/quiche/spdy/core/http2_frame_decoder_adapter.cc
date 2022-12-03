// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/spdy/core/http2_frame_decoder_adapter.h"

// Logging policy: If an error in the input is detected, QUICHE_VLOG(n) is used
// so that the option exists to debug the situation. Otherwise, this code mostly
// uses QUICHE_DVLOG so that the logging does not slow down production code when
// things are working OK.

#include <stddef.h>

#include <cstdint>
#include <cstring>
#include <utility>

#include "quiche/http2/decoder/decode_buffer.h"
#include "quiche/http2/decoder/decode_status.h"
#include "quiche/http2/decoder/http2_frame_decoder.h"
#include "quiche/http2/decoder/http2_frame_decoder_listener.h"
#include "quiche/http2/http2_constants.h"
#include "quiche/http2/http2_structures.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_endian.h"
#include "quiche/spdy/core/hpack/hpack_decoder_adapter.h"
#include "quiche/spdy/core/hpack/hpack_header_table.h"
#include "quiche/spdy/core/spdy_alt_svc_wire_format.h"
#include "quiche/spdy/core/spdy_headers_handler_interface.h"
#include "quiche/spdy/core/spdy_protocol.h"

using ::spdy::ExtensionVisitorInterface;
using ::spdy::HpackDecoderAdapter;
using ::spdy::HpackHeaderTable;
using ::spdy::ParseErrorCode;
using ::spdy::ParseFrameType;
using ::spdy::SpdyAltSvcWireFormat;
using ::spdy::SpdyErrorCode;
using ::spdy::SpdyFramerDebugVisitorInterface;
using ::spdy::SpdyFramerVisitorInterface;
using ::spdy::SpdyFrameType;
using ::spdy::SpdyHeadersHandlerInterface;
using ::spdy::SpdyKnownSettingsId;
using ::spdy::SpdySettingsId;

namespace http2 {
namespace {

const bool kHasPriorityFields = true;
const bool kNotHasPriorityFields = false;

bool IsPaddable(Http2FrameType type) {
  return type == Http2FrameType::DATA || type == Http2FrameType::HEADERS ||
         type == Http2FrameType::PUSH_PROMISE;
}

SpdyFrameType ToSpdyFrameType(Http2FrameType type) {
  return ParseFrameType(static_cast<uint8_t>(type));
}

uint64_t ToSpdyPingId(const Http2PingFields& ping) {
  uint64_t v;
  std::memcpy(&v, ping.opaque_bytes, Http2PingFields::EncodedSize());
  return quiche::QuicheEndian::NetToHost64(v);
}

// Overwrites the fields of the header with invalid values, for the purpose
// of identifying reading of unset fields. Only takes effect for debug builds.
// In Address Sanatizer builds, it also marks the fields as un-readable.
#ifndef NDEBUG
void CorruptFrameHeader(Http2FrameHeader* header) {
  // Beyond a valid payload length, which is 2^24 - 1.
  header->payload_length = 0x1010dead;
  // An unsupported frame type.
  header->type = Http2FrameType(0x80);
  QUICHE_DCHECK(!IsSupportedHttp2FrameType(header->type));
  // Frame flag bits that aren't used by any supported frame type.
  header->flags = Http2FrameFlag(0xd2);
  // A stream id with the reserved high-bit (R in the RFC) set.
  // 2129510127 when the high-bit is cleared.
  header->stream_id = 0xfeedbeef;
}
#else
void CorruptFrameHeader(Http2FrameHeader* /*header*/) {}
#endif

Http2DecoderAdapter::SpdyFramerError HpackDecodingErrorToSpdyFramerError(
    HpackDecodingError error) {
  switch (error) {
    case HpackDecodingError::kOk:
      return Http2DecoderAdapter::SpdyFramerError::SPDY_NO_ERROR;
    case HpackDecodingError::kIndexVarintError:
      return Http2DecoderAdapter::SpdyFramerError::
          SPDY_HPACK_INDEX_VARINT_ERROR;
    case HpackDecodingError::kNameLengthVarintError:
      return Http2DecoderAdapter::SpdyFramerError::
          SPDY_HPACK_NAME_LENGTH_VARINT_ERROR;
    case HpackDecodingError::kValueLengthVarintError:
      return Http2DecoderAdapter::SpdyFramerError::
          SPDY_HPACK_VALUE_LENGTH_VARINT_ERROR;
    case HpackDecodingError::kNameTooLong:
      return Http2DecoderAdapter::SpdyFramerError::SPDY_HPACK_NAME_TOO_LONG;
    case HpackDecodingError::kValueTooLong:
      return Http2DecoderAdapter::SpdyFramerError::SPDY_HPACK_VALUE_TOO_LONG;
    case HpackDecodingError::kNameHuffmanError:
      return Http2DecoderAdapter::SpdyFramerError::
          SPDY_HPACK_NAME_HUFFMAN_ERROR;
    case HpackDecodingError::kValueHuffmanError:
      return Http2DecoderAdapter::SpdyFramerError::
          SPDY_HPACK_VALUE_HUFFMAN_ERROR;
    case HpackDecodingError::kMissingDynamicTableSizeUpdate:
      return Http2DecoderAdapter::SpdyFramerError::
          SPDY_HPACK_MISSING_DYNAMIC_TABLE_SIZE_UPDATE;
    case HpackDecodingError::kInvalidIndex:
      return Http2DecoderAdapter::SpdyFramerError::SPDY_HPACK_INVALID_INDEX;
    case HpackDecodingError::kInvalidNameIndex:
      return Http2DecoderAdapter::SpdyFramerError::
          SPDY_HPACK_INVALID_NAME_INDEX;
    case HpackDecodingError::kDynamicTableSizeUpdateNotAllowed:
      return Http2DecoderAdapter::SpdyFramerError::
          SPDY_HPACK_DYNAMIC_TABLE_SIZE_UPDATE_NOT_ALLOWED;
    case HpackDecodingError::kInitialDynamicTableSizeUpdateIsAboveLowWaterMark:
      return Http2DecoderAdapter::SpdyFramerError::
          SPDY_HPACK_INITIAL_DYNAMIC_TABLE_SIZE_UPDATE_IS_ABOVE_LOW_WATER_MARK;
    case HpackDecodingError::kDynamicTableSizeUpdateIsAboveAcknowledgedSetting:
      return Http2DecoderAdapter::SpdyFramerError::
          SPDY_HPACK_DYNAMIC_TABLE_SIZE_UPDATE_IS_ABOVE_ACKNOWLEDGED_SETTING;
    case HpackDecodingError::kTruncatedBlock:
      return Http2DecoderAdapter::SpdyFramerError::SPDY_HPACK_TRUNCATED_BLOCK;
    case HpackDecodingError::kFragmentTooLong:
      return Http2DecoderAdapter::SpdyFramerError::SPDY_HPACK_FRAGMENT_TOO_LONG;
    case HpackDecodingError::kCompressedHeaderSizeExceedsLimit:
      return Http2DecoderAdapter::SpdyFramerError::
          SPDY_HPACK_COMPRESSED_HEADER_SIZE_EXCEEDS_LIMIT;
  }

  return Http2DecoderAdapter::SpdyFramerError::SPDY_DECOMPRESS_FAILURE;
}

}  // namespace

const char* Http2DecoderAdapter::StateToString(int state) {
  switch (state) {
    case SPDY_ERROR:
      return "ERROR";
    case SPDY_FRAME_COMPLETE:
      return "FRAME_COMPLETE";
    case SPDY_READY_FOR_FRAME:
      return "READY_FOR_FRAME";
    case SPDY_READING_COMMON_HEADER:
      return "READING_COMMON_HEADER";
    case SPDY_CONTROL_FRAME_PAYLOAD:
      return "CONTROL_FRAME_PAYLOAD";
    case SPDY_READ_DATA_FRAME_PADDING_LENGTH:
      return "SPDY_READ_DATA_FRAME_PADDING_LENGTH";
    case SPDY_CONSUME_PADDING:
      return "SPDY_CONSUME_PADDING";
    case SPDY_IGNORE_REMAINING_PAYLOAD:
      return "IGNORE_REMAINING_PAYLOAD";
    case SPDY_FORWARD_STREAM_FRAME:
      return "FORWARD_STREAM_FRAME";
    case SPDY_CONTROL_FRAME_BEFORE_HEADER_BLOCK:
      return "SPDY_CONTROL_FRAME_BEFORE_HEADER_BLOCK";
    case SPDY_CONTROL_FRAME_HEADER_BLOCK:
      return "SPDY_CONTROL_FRAME_HEADER_BLOCK";
    case SPDY_GOAWAY_FRAME_PAYLOAD:
      return "SPDY_GOAWAY_FRAME_PAYLOAD";
    case SPDY_SETTINGS_FRAME_HEADER:
      return "SPDY_SETTINGS_FRAME_HEADER";
    case SPDY_SETTINGS_FRAME_PAYLOAD:
      return "SPDY_SETTINGS_FRAME_PAYLOAD";
    case SPDY_ALTSVC_FRAME_PAYLOAD:
      return "SPDY_ALTSVC_FRAME_PAYLOAD";
  }
  return "UNKNOWN_STATE";
}

const char* Http2DecoderAdapter::SpdyFramerErrorToString(
    SpdyFramerError spdy_framer_error) {
  switch (spdy_framer_error) {
    case SPDY_NO_ERROR:
      return "NO_ERROR";
    case SPDY_INVALID_STREAM_ID:
      return "INVALID_STREAM_ID";
    case SPDY_INVALID_CONTROL_FRAME:
      return "INVALID_CONTROL_FRAME";
    case SPDY_CONTROL_PAYLOAD_TOO_LARGE:
      return "CONTROL_PAYLOAD_TOO_LARGE";
    case SPDY_DECOMPRESS_FAILURE:
      return "DECOMPRESS_FAILURE";
    case SPDY_INVALID_PADDING:
      return "INVALID_PADDING";
    case SPDY_INVALID_DATA_FRAME_FLAGS:
      return "INVALID_DATA_FRAME_FLAGS";
    case SPDY_UNEXPECTED_FRAME:
      return "UNEXPECTED_FRAME";
    case SPDY_INTERNAL_FRAMER_ERROR:
      return "INTERNAL_FRAMER_ERROR";
    case SPDY_INVALID_CONTROL_FRAME_SIZE:
      return "INVALID_CONTROL_FRAME_SIZE";
    case SPDY_OVERSIZED_PAYLOAD:
      return "OVERSIZED_PAYLOAD";
    case SPDY_HPACK_INDEX_VARINT_ERROR:
      return "HPACK_INDEX_VARINT_ERROR";
    case SPDY_HPACK_NAME_LENGTH_VARINT_ERROR:
      return "HPACK_NAME_LENGTH_VARINT_ERROR";
    case SPDY_HPACK_VALUE_LENGTH_VARINT_ERROR:
      return "HPACK_VALUE_LENGTH_VARINT_ERROR";
    case SPDY_HPACK_NAME_TOO_LONG:
      return "HPACK_NAME_TOO_LONG";
    case SPDY_HPACK_VALUE_TOO_LONG:
      return "HPACK_VALUE_TOO_LONG";
    case SPDY_HPACK_NAME_HUFFMAN_ERROR:
      return "HPACK_NAME_HUFFMAN_ERROR";
    case SPDY_HPACK_VALUE_HUFFMAN_ERROR:
      return "HPACK_VALUE_HUFFMAN_ERROR";
    case SPDY_HPACK_MISSING_DYNAMIC_TABLE_SIZE_UPDATE:
      return "HPACK_MISSING_DYNAMIC_TABLE_SIZE_UPDATE";
    case SPDY_HPACK_INVALID_INDEX:
      return "HPACK_INVALID_INDEX";
    case SPDY_HPACK_INVALID_NAME_INDEX:
      return "HPACK_INVALID_NAME_INDEX";
    case SPDY_HPACK_DYNAMIC_TABLE_SIZE_UPDATE_NOT_ALLOWED:
      return "HPACK_DYNAMIC_TABLE_SIZE_UPDATE_NOT_ALLOWED";
    case SPDY_HPACK_INITIAL_DYNAMIC_TABLE_SIZE_UPDATE_IS_ABOVE_LOW_WATER_MARK:
      return "HPACK_INITIAL_DYNAMIC_TABLE_SIZE_UPDATE_IS_ABOVE_LOW_WATER_MARK";
    case SPDY_HPACK_DYNAMIC_TABLE_SIZE_UPDATE_IS_ABOVE_ACKNOWLEDGED_SETTING:
      return "HPACK_DYNAMIC_TABLE_SIZE_UPDATE_IS_ABOVE_ACKNOWLEDGED_SETTING";
    case SPDY_HPACK_TRUNCATED_BLOCK:
      return "HPACK_TRUNCATED_BLOCK";
    case SPDY_HPACK_FRAGMENT_TOO_LONG:
      return "HPACK_FRAGMENT_TOO_LONG";
    case SPDY_HPACK_COMPRESSED_HEADER_SIZE_EXCEEDS_LIMIT:
      return "HPACK_COMPRESSED_HEADER_SIZE_EXCEEDS_LIMIT";
    case SPDY_STOP_PROCESSING:
      return "STOP_PROCESSING";
    case LAST_ERROR:
      return "UNKNOWN_ERROR";
  }
  return "UNKNOWN_ERROR";
}

Http2DecoderAdapter::Http2DecoderAdapter() : frame_decoder_(this) {
  QUICHE_DVLOG(1) << "Http2DecoderAdapter ctor";

  CorruptFrameHeader(&frame_header_);
  CorruptFrameHeader(&hpack_first_frame_header_);
}

Http2DecoderAdapter::~Http2DecoderAdapter() = default;

void Http2DecoderAdapter::set_visitor(SpdyFramerVisitorInterface* visitor) {
  visitor_ = visitor;
}

void Http2DecoderAdapter::set_debug_visitor(
    SpdyFramerDebugVisitorInterface* debug_visitor) {
  debug_visitor_ = debug_visitor;
}

void Http2DecoderAdapter::set_extension_visitor(
    ExtensionVisitorInterface* visitor) {
  extension_ = visitor;
}

size_t Http2DecoderAdapter::ProcessInput(const char* data, size_t len) {
  size_t total_processed = 0;
  while (len > 0 && spdy_state_ != SPDY_ERROR) {
    // Process one at a time so that we update the adapter's internal
    // state appropriately.
    const size_t processed = ProcessInputFrame(data, len);

    // We had some data, and weren't in an error state, so should have
    // processed/consumed at least one byte of it, even if we then ended up
    // in an error state.
    QUICHE_DCHECK(processed > 0)
        << "processed=" << processed << "   spdy_state_=" << spdy_state_
        << "   spdy_framer_error_=" << spdy_framer_error_;

    data += processed;
    len -= processed;
    total_processed += processed;
    if (processed == 0) {
      break;
    }
  }
  return total_processed;
}

Http2DecoderAdapter::SpdyState Http2DecoderAdapter::state() const {
  return spdy_state_;
}

Http2DecoderAdapter::SpdyFramerError Http2DecoderAdapter::spdy_framer_error()
    const {
  return spdy_framer_error_;
}

bool Http2DecoderAdapter::probable_http_response() const {
  return latched_probable_http_response_;
}

void Http2DecoderAdapter::StopProcessing() {
  SetSpdyErrorAndNotify(SpdyFramerError::SPDY_STOP_PROCESSING,
                        "Ignoring further events on this connection.");
}

void Http2DecoderAdapter::SetMaxFrameSize(size_t max_frame_size) {
  max_frame_size_ = max_frame_size;
  frame_decoder_.set_maximum_payload_size(max_frame_size);
}

// ===========================================================================
// Implementations of the methods declared by Http2FrameDecoderListener.

// Called once the common frame header has been decoded for any frame.
// This function is largely based on Http2DecoderAdapter::ValidateFrameHeader
// and some parts of Http2DecoderAdapter::ProcessCommonHeader.
bool Http2DecoderAdapter::OnFrameHeader(const Http2FrameHeader& header) {
  QUICHE_DVLOG(1) << "OnFrameHeader: " << header;
  decoded_frame_header_ = true;
  if (!latched_probable_http_response_) {
    latched_probable_http_response_ = header.IsProbableHttpResponse();
  }
  const uint8_t raw_frame_type = static_cast<uint8_t>(header.type);
  visitor()->OnCommonHeader(header.stream_id, header.payload_length,
                            raw_frame_type, header.flags);
  if (has_expected_frame_type_ && header.type != expected_frame_type_) {
    // Report an unexpected frame error and close the connection if we
    // expect a known frame type (probably CONTINUATION) and receive an
    // unknown frame.
    QUICHE_VLOG(1) << "The framer was expecting to receive a "
                   << expected_frame_type_
                   << " frame, but instead received an unknown frame of type "
                   << header.type;
    SetSpdyErrorAndNotify(SpdyFramerError::SPDY_UNEXPECTED_FRAME, "");
    return false;
  }
  if (!IsSupportedHttp2FrameType(header.type)) {
    if (extension_ != nullptr) {
      // Unknown frames will be passed to the registered extension.
      return true;
    }
    // In HTTP2 we ignore unknown frame types for extensibility, as long as
    // the rest of the control frame header is valid.
    // We rely on the visitor to check validity of stream_id.
    bool valid_stream =
        visitor()->OnUnknownFrame(header.stream_id, raw_frame_type);
    if (!valid_stream) {
      // Report an invalid frame error if the stream_id is not valid.
      QUICHE_VLOG(1) << "Unknown control frame type " << header.type
                     << " received on invalid stream " << header.stream_id;
      SetSpdyErrorAndNotify(SpdyFramerError::SPDY_INVALID_CONTROL_FRAME, "");
      return false;
    } else {
      QUICHE_DVLOG(1) << "Ignoring unknown frame type " << header.type;
      return true;
    }
  }

  SpdyFrameType frame_type = ToSpdyFrameType(header.type);
  if (!IsValidHTTP2FrameStreamId(header.stream_id, frame_type)) {
    QUICHE_VLOG(1) << "The framer received an invalid streamID of "
                   << header.stream_id << " for a frame of type "
                   << header.type;
    SetSpdyErrorAndNotify(SpdyFramerError::SPDY_INVALID_STREAM_ID, "");
    return false;
  }

  if (has_expected_frame_type_ && header.type != expected_frame_type_) {
    QUICHE_VLOG(1) << "Expected frame type " << expected_frame_type_ << ", not "
                   << header.type;
    SetSpdyErrorAndNotify(SpdyFramerError::SPDY_UNEXPECTED_FRAME, "");
    return false;
  }

  if (!has_expected_frame_type_ &&
      header.type == Http2FrameType::CONTINUATION) {
    QUICHE_VLOG(1) << "Got CONTINUATION frame when not expected.";
    SetSpdyErrorAndNotify(SpdyFramerError::SPDY_UNEXPECTED_FRAME, "");
    return false;
  }

  if (header.type == Http2FrameType::DATA) {
    // For some reason SpdyFramer still rejects invalid DATA frame flags.
    uint8_t valid_flags = Http2FrameFlag::PADDED | Http2FrameFlag::END_STREAM;
    if (header.HasAnyFlags(~valid_flags)) {
      SetSpdyErrorAndNotify(SpdyFramerError::SPDY_INVALID_DATA_FRAME_FLAGS, "");
      return false;
    }
  }

  return true;
}

void Http2DecoderAdapter::OnDataStart(const Http2FrameHeader& header) {
  QUICHE_DVLOG(1) << "OnDataStart: " << header;

  if (IsOkToStartFrame(header) && HasRequiredStreamId(header)) {
    frame_header_ = header;
    has_frame_header_ = true;
    visitor()->OnDataFrameHeader(header.stream_id, header.payload_length,
                                 header.IsEndStream());
  }
}

void Http2DecoderAdapter::OnDataPayload(const char* data, size_t len) {
  QUICHE_DVLOG(1) << "OnDataPayload: len=" << len;
  QUICHE_DCHECK(has_frame_header_);
  QUICHE_DCHECK_EQ(frame_header_.type, Http2FrameType::DATA);
  visitor()->OnStreamFrameData(frame_header().stream_id, data, len);
}

void Http2DecoderAdapter::OnDataEnd() {
  QUICHE_DVLOG(1) << "OnDataEnd";
  QUICHE_DCHECK(has_frame_header_);
  QUICHE_DCHECK_EQ(frame_header_.type, Http2FrameType::DATA);
  if (frame_header().IsEndStream()) {
    visitor()->OnStreamEnd(frame_header().stream_id);
  }
  opt_pad_length_.reset();
}

void Http2DecoderAdapter::OnHeadersStart(const Http2FrameHeader& header) {
  QUICHE_DVLOG(1) << "OnHeadersStart: " << header;
  if (IsOkToStartFrame(header) && HasRequiredStreamId(header)) {
    frame_header_ = header;
    has_frame_header_ = true;
    if (header.HasPriority()) {
      // Once we've got the priority fields, then we can report the arrival
      // of this HEADERS frame.
      on_headers_called_ = false;
      return;
    }
    on_headers_called_ = true;
    ReportReceiveCompressedFrame(header);
    visitor()->OnHeaders(header.stream_id, header.payload_length,
                         kNotHasPriorityFields,
                         0,      // priority
                         0,      // parent_stream_id
                         false,  // exclusive
                         header.IsEndStream(), header.IsEndHeaders());
    CommonStartHpackBlock();
  }
}

void Http2DecoderAdapter::OnHeadersPriority(
    const Http2PriorityFields& priority) {
  QUICHE_DVLOG(1) << "OnHeadersPriority: " << priority;
  QUICHE_DCHECK(has_frame_header_);
  QUICHE_DCHECK_EQ(frame_type(), Http2FrameType::HEADERS) << frame_header_;
  QUICHE_DCHECK(frame_header_.HasPriority());
  QUICHE_DCHECK(!on_headers_called_);
  on_headers_called_ = true;
  ReportReceiveCompressedFrame(frame_header_);
  if (!visitor()) {
    QUICHE_BUG(spdy_bug_1_1)
        << "Visitor is nullptr, handling priority in headers failed."
        << " priority:" << priority << " frame_header:" << frame_header_;
    return;
  }
  visitor()->OnHeaders(
      frame_header_.stream_id, frame_header_.payload_length, kHasPriorityFields,
      priority.weight, priority.stream_dependency, priority.is_exclusive,
      frame_header_.IsEndStream(), frame_header_.IsEndHeaders());
  CommonStartHpackBlock();
}

void Http2DecoderAdapter::OnHpackFragment(const char* data, size_t len) {
  QUICHE_DVLOG(1) << "OnHpackFragment: len=" << len;
  on_hpack_fragment_called_ = true;
  auto* decoder = GetHpackDecoder();
  if (!decoder->HandleControlFrameHeadersData(data, len)) {
    SetSpdyErrorAndNotify(HpackDecodingErrorToSpdyFramerError(decoder->error()),
                          decoder->detailed_error());
    return;
  }
}

void Http2DecoderAdapter::OnHeadersEnd() {
  QUICHE_DVLOG(1) << "OnHeadersEnd";
  CommonHpackFragmentEnd();
  opt_pad_length_.reset();
}

void Http2DecoderAdapter::OnPriorityFrame(const Http2FrameHeader& header,
                                          const Http2PriorityFields& priority) {
  QUICHE_DVLOG(1) << "OnPriorityFrame: " << header
                  << "; priority: " << priority;
  if (IsOkToStartFrame(header) && HasRequiredStreamId(header)) {
    visitor()->OnPriority(header.stream_id, priority.stream_dependency,
                          priority.weight, priority.is_exclusive);
  }
}

void Http2DecoderAdapter::OnContinuationStart(const Http2FrameHeader& header) {
  QUICHE_DVLOG(1) << "OnContinuationStart: " << header;
  if (IsOkToStartFrame(header) && HasRequiredStreamId(header)) {
    QUICHE_DCHECK(has_hpack_first_frame_header_);
    if (header.stream_id != hpack_first_frame_header_.stream_id) {
      SetSpdyErrorAndNotify(SpdyFramerError::SPDY_UNEXPECTED_FRAME, "");
      return;
    }
    frame_header_ = header;
    has_frame_header_ = true;
    ReportReceiveCompressedFrame(header);
    visitor()->OnContinuation(header.stream_id, header.payload_length,
                              header.IsEndHeaders());
  }
}

void Http2DecoderAdapter::OnContinuationEnd() {
  QUICHE_DVLOG(1) << "OnContinuationEnd";
  CommonHpackFragmentEnd();
}

void Http2DecoderAdapter::OnPadLength(size_t trailing_length) {
  QUICHE_DVLOG(1) << "OnPadLength: " << trailing_length;
  opt_pad_length_ = trailing_length;
  QUICHE_DCHECK_LT(trailing_length, 256u);
  if (frame_header_.type == Http2FrameType::DATA) {
    visitor()->OnStreamPadLength(stream_id(), trailing_length);
  }
}

void Http2DecoderAdapter::OnPadding(const char* /*padding*/,
                                    size_t skipped_length) {
  QUICHE_DVLOG(1) << "OnPadding: " << skipped_length;
  if (frame_header_.type == Http2FrameType::DATA) {
    visitor()->OnStreamPadding(stream_id(), skipped_length);
  } else {
    MaybeAnnounceEmptyFirstHpackFragment();
  }
}

void Http2DecoderAdapter::OnRstStream(const Http2FrameHeader& header,
                                      Http2ErrorCode http2_error_code) {
  QUICHE_DVLOG(1) << "OnRstStream: " << header << "; code=" << http2_error_code;
  if (IsOkToStartFrame(header) && HasRequiredStreamId(header)) {
    SpdyErrorCode error_code =
        ParseErrorCode(static_cast<uint32_t>(http2_error_code));
    visitor()->OnRstStream(header.stream_id, error_code);
  }
}

void Http2DecoderAdapter::OnSettingsStart(const Http2FrameHeader& header) {
  QUICHE_DVLOG(1) << "OnSettingsStart: " << header;
  if (IsOkToStartFrame(header) && HasRequiredStreamIdZero(header)) {
    frame_header_ = header;
    has_frame_header_ = true;
    visitor()->OnSettings();
  }
}

void Http2DecoderAdapter::OnSetting(const Http2SettingFields& setting_fields) {
  QUICHE_DVLOG(1) << "OnSetting: " << setting_fields;
  const auto parameter = static_cast<SpdySettingsId>(setting_fields.parameter);
  visitor()->OnSetting(parameter, setting_fields.value);
  SpdyKnownSettingsId known_id;
  if (extension_ != nullptr && !spdy::ParseSettingsId(parameter, &known_id)) {
    extension_->OnSetting(parameter, setting_fields.value);
  }
}

void Http2DecoderAdapter::OnSettingsEnd() {
  QUICHE_DVLOG(1) << "OnSettingsEnd";
  visitor()->OnSettingsEnd();
}

void Http2DecoderAdapter::OnSettingsAck(const Http2FrameHeader& header) {
  QUICHE_DVLOG(1) << "OnSettingsAck: " << header;
  if (IsOkToStartFrame(header) && HasRequiredStreamIdZero(header)) {
    visitor()->OnSettingsAck();
  }
}

void Http2DecoderAdapter::OnPushPromiseStart(
    const Http2FrameHeader& header, const Http2PushPromiseFields& promise,
    size_t total_padding_length) {
  QUICHE_DVLOG(1) << "OnPushPromiseStart: " << header
                  << "; promise: " << promise
                  << "; total_padding_length: " << total_padding_length;
  if (IsOkToStartFrame(header) && HasRequiredStreamId(header)) {
    if (promise.promised_stream_id == 0) {
      SetSpdyErrorAndNotify(SpdyFramerError::SPDY_INVALID_CONTROL_FRAME, "");
      return;
    }
    frame_header_ = header;
    has_frame_header_ = true;
    ReportReceiveCompressedFrame(header);
    visitor()->OnPushPromise(header.stream_id, promise.promised_stream_id,
                             header.IsEndHeaders());
    CommonStartHpackBlock();
  }
}

void Http2DecoderAdapter::OnPushPromiseEnd() {
  QUICHE_DVLOG(1) << "OnPushPromiseEnd";
  CommonHpackFragmentEnd();
  opt_pad_length_.reset();
}

void Http2DecoderAdapter::OnPing(const Http2FrameHeader& header,
                                 const Http2PingFields& ping) {
  QUICHE_DVLOG(1) << "OnPing: " << header << "; ping: " << ping;
  if (IsOkToStartFrame(header) && HasRequiredStreamIdZero(header)) {
    visitor()->OnPing(ToSpdyPingId(ping), false);
  }
}

void Http2DecoderAdapter::OnPingAck(const Http2FrameHeader& header,
                                    const Http2PingFields& ping) {
  QUICHE_DVLOG(1) << "OnPingAck: " << header << "; ping: " << ping;
  if (IsOkToStartFrame(header) && HasRequiredStreamIdZero(header)) {
    visitor()->OnPing(ToSpdyPingId(ping), true);
  }
}

void Http2DecoderAdapter::OnGoAwayStart(const Http2FrameHeader& header,
                                        const Http2GoAwayFields& goaway) {
  QUICHE_DVLOG(1) << "OnGoAwayStart: " << header << "; goaway: " << goaway;
  if (IsOkToStartFrame(header) && HasRequiredStreamIdZero(header)) {
    frame_header_ = header;
    has_frame_header_ = true;
    SpdyErrorCode error_code =
        ParseErrorCode(static_cast<uint32_t>(goaway.error_code));
    visitor()->OnGoAway(goaway.last_stream_id, error_code);
  }
}

void Http2DecoderAdapter::OnGoAwayOpaqueData(const char* data, size_t len) {
  QUICHE_DVLOG(1) << "OnGoAwayOpaqueData: len=" << len;
  visitor()->OnGoAwayFrameData(data, len);
}

void Http2DecoderAdapter::OnGoAwayEnd() {
  QUICHE_DVLOG(1) << "OnGoAwayEnd";
  visitor()->OnGoAwayFrameData(nullptr, 0);
}

void Http2DecoderAdapter::OnWindowUpdate(const Http2FrameHeader& header,
                                         uint32_t increment) {
  QUICHE_DVLOG(1) << "OnWindowUpdate: " << header
                  << "; increment=" << increment;
  if (IsOkToStartFrame(header)) {
    visitor()->OnWindowUpdate(header.stream_id, increment);
  }
}

// Per RFC7838, an ALTSVC frame on stream 0 with origin_length == 0, or one on
// a stream other than stream 0 with origin_length != 0 MUST be ignored.  All
// frames are decoded by Http2DecoderAdapter, and it is left to the consumer
// (listener) to implement this behavior.
void Http2DecoderAdapter::OnAltSvcStart(const Http2FrameHeader& header,
                                        size_t origin_length,
                                        size_t value_length) {
  QUICHE_DVLOG(1) << "OnAltSvcStart: " << header
                  << "; origin_length: " << origin_length
                  << "; value_length: " << value_length;
  if (!IsOkToStartFrame(header)) {
    return;
  }
  frame_header_ = header;
  has_frame_header_ = true;
  alt_svc_origin_.clear();
  alt_svc_value_.clear();
}

void Http2DecoderAdapter::OnAltSvcOriginData(const char* data, size_t len) {
  QUICHE_DVLOG(1) << "OnAltSvcOriginData: len=" << len;
  alt_svc_origin_.append(data, len);
}

// Called when decoding the Alt-Svc-Field-Value of an ALTSVC;
// the field is uninterpreted.
void Http2DecoderAdapter::OnAltSvcValueData(const char* data, size_t len) {
  QUICHE_DVLOG(1) << "OnAltSvcValueData: len=" << len;
  alt_svc_value_.append(data, len);
}

void Http2DecoderAdapter::OnAltSvcEnd() {
  QUICHE_DVLOG(1) << "OnAltSvcEnd: origin.size(): " << alt_svc_origin_.size()
                  << "; value.size(): " << alt_svc_value_.size();
  SpdyAltSvcWireFormat::AlternativeServiceVector altsvc_vector;
  if (!SpdyAltSvcWireFormat::ParseHeaderFieldValue(alt_svc_value_,
                                                   &altsvc_vector)) {
    QUICHE_DLOG(ERROR) << "SpdyAltSvcWireFormat::ParseHeaderFieldValue failed.";
    SetSpdyErrorAndNotify(SpdyFramerError::SPDY_INVALID_CONTROL_FRAME, "");
    return;
  }
  visitor()->OnAltSvc(frame_header_.stream_id, alt_svc_origin_, altsvc_vector);
  // We assume that ALTSVC frames are rare, so get rid of the storage.
  alt_svc_origin_.clear();
  alt_svc_origin_.shrink_to_fit();
  alt_svc_value_.clear();
  alt_svc_value_.shrink_to_fit();
}

void Http2DecoderAdapter::OnPriorityUpdateStart(
    const Http2FrameHeader& header,
    const Http2PriorityUpdateFields& priority_update) {
  QUICHE_DVLOG(1) << "OnPriorityUpdateStart: " << header
                  << "; prioritized_stream_id: "
                  << priority_update.prioritized_stream_id;
  if (IsOkToStartFrame(header) && HasRequiredStreamIdZero(header) &&
      HasRequiredStreamId(priority_update.prioritized_stream_id)) {
    frame_header_ = header;
    has_frame_header_ = true;
    prioritized_stream_id_ = priority_update.prioritized_stream_id;
  }
}

void Http2DecoderAdapter::OnPriorityUpdatePayload(const char* data,
                                                  size_t len) {
  QUICHE_DVLOG(1) << "OnPriorityUpdatePayload: len=" << len;
  priority_field_value_.append(data, len);
}

void Http2DecoderAdapter::OnPriorityUpdateEnd() {
  QUICHE_DVLOG(1) << "OnPriorityUpdateEnd: priority_field_value.size(): "
                  << priority_field_value_.size();
  visitor()->OnPriorityUpdate(prioritized_stream_id_, priority_field_value_);
  priority_field_value_.clear();
}

void Http2DecoderAdapter::OnUnknownStart(const Http2FrameHeader& header) {
  QUICHE_DVLOG(1) << "OnUnknownStart: " << header;
  if (IsOkToStartFrame(header)) {
    frame_header_ = header;
    has_frame_header_ = true;
    const uint8_t type = static_cast<uint8_t>(header.type);
    const uint8_t flags = static_cast<uint8_t>(header.flags);
    if (extension_ != nullptr) {
      handling_extension_payload_ = extension_->OnFrameHeader(
          header.stream_id, header.payload_length, type, flags);
    }
    visitor()->OnUnknownFrameStart(header.stream_id, header.payload_length,
                                   type, flags);
  }
}

void Http2DecoderAdapter::OnUnknownPayload(const char* data, size_t len) {
  if (handling_extension_payload_) {
    extension_->OnFramePayload(data, len);
  } else {
    QUICHE_DVLOG(1) << "OnUnknownPayload: len=" << len;
  }
  visitor()->OnUnknownFramePayload(frame_header_.stream_id,
                                   absl::string_view(data, len));
}

void Http2DecoderAdapter::OnUnknownEnd() {
  QUICHE_DVLOG(1) << "OnUnknownEnd";
  handling_extension_payload_ = false;
}

void Http2DecoderAdapter::OnPaddingTooLong(const Http2FrameHeader& header,
                                           size_t missing_length) {
  QUICHE_DVLOG(1) << "OnPaddingTooLong: " << header
                  << "; missing_length: " << missing_length;
  if (header.type == Http2FrameType::DATA) {
    if (header.payload_length == 0) {
      QUICHE_DCHECK_EQ(1u, missing_length);
      SetSpdyErrorAndNotify(SpdyFramerError::SPDY_INVALID_DATA_FRAME_FLAGS, "");
      return;
    }
    visitor()->OnStreamPadding(header.stream_id, 1);
  }
  SetSpdyErrorAndNotify(SpdyFramerError::SPDY_INVALID_PADDING, "");
}

void Http2DecoderAdapter::OnFrameSizeError(const Http2FrameHeader& header) {
  QUICHE_DVLOG(1) << "OnFrameSizeError: " << header;
  if (header.payload_length > max_frame_size_) {
    if (header.type == Http2FrameType::DATA) {
      SetSpdyErrorAndNotify(SpdyFramerError::SPDY_OVERSIZED_PAYLOAD, "");
    } else {
      SetSpdyErrorAndNotify(SpdyFramerError::SPDY_CONTROL_PAYLOAD_TOO_LARGE,
                            "");
    }
    return;
  }
  switch (header.type) {
    case Http2FrameType::GOAWAY:
    case Http2FrameType::ALTSVC:
      SetSpdyErrorAndNotify(SpdyFramerError::SPDY_INVALID_CONTROL_FRAME, "");
      break;
    default:
      SetSpdyErrorAndNotify(SpdyFramerError::SPDY_INVALID_CONTROL_FRAME_SIZE,
                            "");
  }
}

// Decodes the input up to the next frame boundary (i.e. at most one frame),
// stopping early if an error is detected.
size_t Http2DecoderAdapter::ProcessInputFrame(const char* data, size_t len) {
  QUICHE_DCHECK_NE(spdy_state_, SpdyState::SPDY_ERROR);
  DecodeBuffer db(data, len);
  DecodeStatus status = frame_decoder_.DecodeFrame(&db);
  if (spdy_state_ != SpdyState::SPDY_ERROR) {
    DetermineSpdyState(status);
  } else {
    QUICHE_VLOG(1) << "ProcessInputFrame spdy_framer_error_="
                   << SpdyFramerErrorToString(spdy_framer_error_);
    if (spdy_framer_error_ == SpdyFramerError::SPDY_INVALID_PADDING &&
        has_frame_header_ && frame_type() != Http2FrameType::DATA) {
      // spdy_framer_test checks that all of the available frame payload
      // has been consumed, so do that.
      size_t total = remaining_total_payload();
      if (total <= frame_header().payload_length) {
        size_t avail = db.MinLengthRemaining(total);
        QUICHE_VLOG(1) << "Skipping past " << avail << " bytes, of " << total
                       << " total remaining in the frame's payload.";
        db.AdvanceCursor(avail);
      } else {
        QUICHE_BUG(spdy_bug_1_2)
            << "Total remaining (" << total
            << ") should not be greater than the payload length; "
            << frame_header();
      }
    }
  }
  return db.Offset();
}

// After decoding, determine the next SpdyState. Only called if the current
// state is NOT SpdyState::SPDY_ERROR (i.e. if none of the callback methods
// detected an error condition), because otherwise we assume that the callback
// method has set spdy_framer_error_ appropriately.
void Http2DecoderAdapter::DetermineSpdyState(DecodeStatus status) {
  QUICHE_DCHECK_EQ(spdy_framer_error_, SPDY_NO_ERROR);
  QUICHE_DCHECK(!HasError()) << spdy_framer_error_;
  switch (status) {
    case DecodeStatus::kDecodeDone:
      QUICHE_DVLOG(1) << "ProcessInputFrame -> DecodeStatus::kDecodeDone";
      ResetBetweenFrames();
      break;
    case DecodeStatus::kDecodeInProgress:
      QUICHE_DVLOG(1) << "ProcessInputFrame -> DecodeStatus::kDecodeInProgress";
      if (decoded_frame_header_) {
        if (IsDiscardingPayload()) {
          set_spdy_state(SpdyState::SPDY_IGNORE_REMAINING_PAYLOAD);
        } else if (has_frame_header_ && frame_type() == Http2FrameType::DATA) {
          if (IsReadingPaddingLength()) {
            set_spdy_state(SpdyState::SPDY_READ_DATA_FRAME_PADDING_LENGTH);
          } else if (IsSkippingPadding()) {
            set_spdy_state(SpdyState::SPDY_CONSUME_PADDING);
          } else {
            set_spdy_state(SpdyState::SPDY_FORWARD_STREAM_FRAME);
          }
        } else {
          set_spdy_state(SpdyState::SPDY_CONTROL_FRAME_PAYLOAD);
        }
      } else {
        set_spdy_state(SpdyState::SPDY_READING_COMMON_HEADER);
      }
      break;
    case DecodeStatus::kDecodeError:
      QUICHE_VLOG(1) << "ProcessInputFrame -> DecodeStatus::kDecodeError";
      if (IsDiscardingPayload()) {
        if (remaining_total_payload() == 0) {
          // Push the Http2FrameDecoder out of state kDiscardPayload now
          // since doing so requires no input.
          DecodeBuffer tmp("", 0);
          DecodeStatus decode_status = frame_decoder_.DecodeFrame(&tmp);
          if (decode_status != DecodeStatus::kDecodeDone) {
            QUICHE_BUG(spdy_bug_1_3)
                << "Expected to be done decoding the frame, not "
                << decode_status;
            SetSpdyErrorAndNotify(SPDY_INTERNAL_FRAMER_ERROR, "");
          } else if (spdy_framer_error_ != SPDY_NO_ERROR) {
            QUICHE_BUG(spdy_bug_1_4)
                << "Expected to have no error, not "
                << SpdyFramerErrorToString(spdy_framer_error_);
          } else {
            ResetBetweenFrames();
          }
        } else {
          set_spdy_state(SpdyState::SPDY_IGNORE_REMAINING_PAYLOAD);
        }
      } else {
        SetSpdyErrorAndNotify(SPDY_INVALID_CONTROL_FRAME, "");
      }
      break;
  }
}

void Http2DecoderAdapter::ResetBetweenFrames() {
  CorruptFrameHeader(&frame_header_);
  decoded_frame_header_ = false;
  has_frame_header_ = false;
  set_spdy_state(SpdyState::SPDY_READY_FOR_FRAME);
}

void Http2DecoderAdapter::set_spdy_state(SpdyState v) {
  QUICHE_DVLOG(2) << "set_spdy_state(" << StateToString(v) << ")";
  spdy_state_ = v;
}

void Http2DecoderAdapter::SetSpdyErrorAndNotify(SpdyFramerError error,
                                                std::string detailed_error) {
  if (HasError()) {
    QUICHE_DCHECK_EQ(spdy_state_, SpdyState::SPDY_ERROR);
  } else {
    QUICHE_VLOG(2) << "SetSpdyErrorAndNotify(" << SpdyFramerErrorToString(error)
                   << ")";
    QUICHE_DCHECK_NE(error, SpdyFramerError::SPDY_NO_ERROR);
    spdy_framer_error_ = error;
    set_spdy_state(SpdyState::SPDY_ERROR);
    frame_decoder_.set_listener(&no_op_listener_);
    visitor()->OnError(error, detailed_error);
  }
}

bool Http2DecoderAdapter::HasError() const {
  if (spdy_state_ == SpdyState::SPDY_ERROR) {
    QUICHE_DCHECK_NE(spdy_framer_error(), SpdyFramerError::SPDY_NO_ERROR);
    return true;
  } else {
    QUICHE_DCHECK_EQ(spdy_framer_error(), SpdyFramerError::SPDY_NO_ERROR);
    return false;
  }
}

const Http2FrameHeader& Http2DecoderAdapter::frame_header() const {
  QUICHE_DCHECK(has_frame_header_);
  return frame_header_;
}

uint32_t Http2DecoderAdapter::stream_id() const {
  return frame_header().stream_id;
}

Http2FrameType Http2DecoderAdapter::frame_type() const {
  return frame_header().type;
}

size_t Http2DecoderAdapter::remaining_total_payload() const {
  QUICHE_DCHECK(has_frame_header_);
  size_t remaining = frame_decoder_.remaining_payload();
  if (IsPaddable(frame_type()) && frame_header_.IsPadded()) {
    remaining += frame_decoder_.remaining_padding();
  }
  return remaining;
}

bool Http2DecoderAdapter::IsReadingPaddingLength() {
  bool result = frame_header_.IsPadded() && !opt_pad_length_;
  QUICHE_DVLOG(2) << "Http2DecoderAdapter::IsReadingPaddingLength: " << result;
  return result;
}
bool Http2DecoderAdapter::IsSkippingPadding() {
  bool result = frame_header_.IsPadded() && opt_pad_length_ &&
                frame_decoder_.remaining_payload() == 0 &&
                frame_decoder_.remaining_padding() > 0;
  QUICHE_DVLOG(2) << "Http2DecoderAdapter::IsSkippingPadding: " << result;
  return result;
}
bool Http2DecoderAdapter::IsDiscardingPayload() {
  bool result = decoded_frame_header_ && frame_decoder_.IsDiscardingPayload();
  QUICHE_DVLOG(2) << "Http2DecoderAdapter::IsDiscardingPayload: " << result;
  return result;
}
// Called from OnXyz or OnXyzStart methods to decide whether it is OK to
// handle the callback.
bool Http2DecoderAdapter::IsOkToStartFrame(const Http2FrameHeader& header) {
  QUICHE_DVLOG(3) << "IsOkToStartFrame";
  if (HasError()) {
    QUICHE_VLOG(2) << "HasError()";
    return false;
  }
  QUICHE_DCHECK(!has_frame_header_);
  if (has_expected_frame_type_ && header.type != expected_frame_type_) {
    QUICHE_VLOG(1) << "Expected frame type " << expected_frame_type_ << ", not "
                   << header.type;
    SetSpdyErrorAndNotify(SpdyFramerError::SPDY_UNEXPECTED_FRAME, "");
    return false;
  }

  return true;
}

bool Http2DecoderAdapter::HasRequiredStreamId(uint32_t stream_id) {
  QUICHE_DVLOG(3) << "HasRequiredStreamId: " << stream_id;
  if (HasError()) {
    QUICHE_VLOG(2) << "HasError()";
    return false;
  }
  if (stream_id != 0) {
    return true;
  }
  QUICHE_VLOG(1) << "Stream Id is required, but zero provided";
  SetSpdyErrorAndNotify(SpdyFramerError::SPDY_INVALID_STREAM_ID, "");
  return false;
}

bool Http2DecoderAdapter::HasRequiredStreamId(const Http2FrameHeader& header) {
  return HasRequiredStreamId(header.stream_id);
}

bool Http2DecoderAdapter::HasRequiredStreamIdZero(uint32_t stream_id) {
  QUICHE_DVLOG(3) << "HasRequiredStreamIdZero: " << stream_id;
  if (HasError()) {
    QUICHE_VLOG(2) << "HasError()";
    return false;
  }
  if (stream_id == 0) {
    return true;
  }
  QUICHE_VLOG(1) << "Stream Id was not zero, as required: " << stream_id;
  SetSpdyErrorAndNotify(SpdyFramerError::SPDY_INVALID_STREAM_ID, "");
  return false;
}

bool Http2DecoderAdapter::HasRequiredStreamIdZero(
    const Http2FrameHeader& header) {
  return HasRequiredStreamIdZero(header.stream_id);
}

void Http2DecoderAdapter::ReportReceiveCompressedFrame(
    const Http2FrameHeader& header) {
  if (debug_visitor() != nullptr) {
    size_t total = header.payload_length + Http2FrameHeader::EncodedSize();
    debug_visitor()->OnReceiveCompressedFrame(
        header.stream_id, ToSpdyFrameType(header.type), total);
  }
}

HpackDecoderAdapter* Http2DecoderAdapter::GetHpackDecoder() {
  if (hpack_decoder_ == nullptr) {
    hpack_decoder_ = std::make_unique<HpackDecoderAdapter>();
  }
  return hpack_decoder_.get();
}

void Http2DecoderAdapter::CommonStartHpackBlock() {
  QUICHE_DVLOG(1) << "CommonStartHpackBlock";
  QUICHE_DCHECK(!has_hpack_first_frame_header_);
  if (!frame_header_.IsEndHeaders()) {
    hpack_first_frame_header_ = frame_header_;
    has_hpack_first_frame_header_ = true;
  } else {
    CorruptFrameHeader(&hpack_first_frame_header_);
  }
  on_hpack_fragment_called_ = false;
  SpdyHeadersHandlerInterface* handler =
      visitor()->OnHeaderFrameStart(stream_id());
  if (handler == nullptr) {
    QUICHE_BUG(spdy_bug_1_5) << "visitor_->OnHeaderFrameStart returned nullptr";
    SetSpdyErrorAndNotify(SpdyFramerError::SPDY_INTERNAL_FRAMER_ERROR, "");
    return;
  }
  GetHpackDecoder()->HandleControlFrameHeadersStart(handler);
}

// SpdyFramer calls HandleControlFrameHeadersData even if there are zero
// fragment bytes in the first frame, so do the same.
void Http2DecoderAdapter::MaybeAnnounceEmptyFirstHpackFragment() {
  if (!on_hpack_fragment_called_) {
    OnHpackFragment(nullptr, 0);
    QUICHE_DCHECK(on_hpack_fragment_called_);
  }
}

void Http2DecoderAdapter::CommonHpackFragmentEnd() {
  QUICHE_DVLOG(1) << "CommonHpackFragmentEnd: stream_id=" << stream_id();
  if (HasError()) {
    QUICHE_VLOG(1) << "HasError(), returning";
    return;
  }
  QUICHE_DCHECK(has_frame_header_);
  MaybeAnnounceEmptyFirstHpackFragment();
  if (frame_header_.IsEndHeaders()) {
    QUICHE_DCHECK_EQ(has_hpack_first_frame_header_,
                     frame_type() == Http2FrameType::CONTINUATION)
        << frame_header();
    has_expected_frame_type_ = false;
    auto* decoder = GetHpackDecoder();
    if (decoder->HandleControlFrameHeadersComplete()) {
      visitor()->OnHeaderFrameEnd(stream_id());
    } else {
      SetSpdyErrorAndNotify(
          HpackDecodingErrorToSpdyFramerError(decoder->error()), "");
      return;
    }
    const Http2FrameHeader& first = frame_type() == Http2FrameType::CONTINUATION
                                        ? hpack_first_frame_header_
                                        : frame_header_;
    if (first.type == Http2FrameType::HEADERS && first.IsEndStream()) {
      visitor()->OnStreamEnd(first.stream_id);
    }
    has_hpack_first_frame_header_ = false;
    CorruptFrameHeader(&hpack_first_frame_header_);
  } else {
    QUICHE_DCHECK(has_hpack_first_frame_header_);
    has_expected_frame_type_ = true;
    expected_frame_type_ = Http2FrameType::CONTINUATION;
  }
}

}  // namespace http2

namespace spdy {

bool SpdyFramerVisitorInterface::OnGoAwayFrameData(const char* /*goaway_data*/,
                                                   size_t /*len*/) {
  return true;
}

}  // namespace spdy
