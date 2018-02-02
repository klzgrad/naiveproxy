// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/http/decoder/quic_http_frame_decoder_adapter.h"

// Logging policy: If an error in the input is detected, VLOG(n) is used so that
// the option exists to debug the situation. Otherwise, this code mostly uses
// DVLOG so that the logging does not slow down production code when things are
// working OK.

#include <stddef.h>

#include <cstdint>
#include <cstring>
#include <utility>

#include "base/logging.h"
#include "net/quic/http/decoder/quic_http_decode_buffer.h"
#include "net/quic/http/decoder/quic_http_decode_status.h"
#include "net/quic/http/decoder/quic_http_frame_decoder.h"
#include "net/quic/http/decoder/quic_http_frame_decoder_listener.h"
#include "net/quic/http/quic_http_constants.h"
#include "net/quic/http/quic_http_structures.h"
#include "net/quic/platform/api/quic_ptr_util.h"
#include "net/quic/platform/api/quic_string_utils.h"
#include "net/spdy/core/hpack/hpack_decoder_adapter.h"
#include "net/spdy/core/hpack/hpack_header_table.h"
#include "net/spdy/core/http2_frame_decoder_adapter.h"
#include "net/spdy/core/spdy_alt_svc_wire_format.h"
#include "net/spdy/core/spdy_bug_tracker.h"
#include "net/spdy/core/spdy_header_block.h"
#include "net/spdy/core/spdy_headers_handler_interface.h"
#include "net/spdy/core/spdy_protocol.h"

#if 0
using ::ExtensionVisitorInterface;
using ::HpackDecoderAdapter;
using ::HpackHeaderTable;
using ::ParseErrorCode;
using ::ParseFrameType;
using ::SpdyAltSvcWireFormat;
using ::SpdyErrorCode;
using ::SpdyFrameType;
using ::SpdyFramerDebugVisitorInterface;
using ::SpdyFramerVisitorInterface;
using ::SpdyHeadersHandlerInterface;
using ::SpdySettingsIds;
#endif
using base::nullopt;

namespace net {

using SpdyFramerError = Http2DecoderAdapter::SpdyFramerError;

namespace {

const bool kHasPriorityFields = true;
const bool kNotHasPriorityFields = false;

bool IsPaddable(QuicHttpFrameType type) {
  return type == QuicHttpFrameType::DATA ||
         type == QuicHttpFrameType::HEADERS ||
         type == QuicHttpFrameType::PUSH_PROMISE;
}

SpdyFrameType ToSpdyFrameType(QuicHttpFrameType type) {
  return ParseFrameType(static_cast<uint8_t>(type));
}

uint64_t ToSpdyPingId(const QuicHttpPingFields& ping) {
  uint64_t v;
  std::memcpy(&v, ping.opaque_bytes, QuicHttpPingFields::EncodedSize());
  return base::NetToHost64(v);
}

// Overwrites the fields of the header with invalid values, for the purpose
// of identifying reading of unset fields. Only takes effect for debug builds.
// In Address Sanatizer builds, it also marks the fields as un-readable.
void CorruptFrameHeader(QuicHttpFrameHeader* header) {
#ifndef NDEBUG
  // Beyond a valid payload length, which is 2^24 - 1.
  header->payload_length = 0x1010dead;
  // An unsupported frame type.
  header->type = QuicHttpFrameType(0x80);
  DCHECK(!IsSupportedQuicHttpFrameType(header->type));
  // Frame flag bits that aren't used by any supported frame type.
  header->flags = QuicHttpFrameFlag(0xd2);
  // A stream id with the reserved high-bit (R in the RFC) set.
  // 2129510127 when the high-bit is cleared.
  header->stream_id = 0xfeedbeef;
#endif
}

}  // namespace

const char* QuicHttpDecoderAdapter::StateToString(int state) {
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

QuicHttpDecoderAdapter::QuicHttpDecoderAdapter() {
  DVLOG(1) << "QuicHttpDecoderAdapter ctor";
  ResetInternal();
}

QuicHttpDecoderAdapter::~QuicHttpDecoderAdapter() {}

void QuicHttpDecoderAdapter::set_visitor(SpdyFramerVisitorInterface* visitor) {
  visitor_ = visitor;
}

void QuicHttpDecoderAdapter::set_debug_visitor(
    SpdyFramerDebugVisitorInterface* debug_visitor) {
  debug_visitor_ = debug_visitor;
}

void QuicHttpDecoderAdapter::set_process_single_input_frame(bool v) {
  process_single_input_frame_ = v;
}

void QuicHttpDecoderAdapter::set_extension_visitor(
    ExtensionVisitorInterface* visitor) {
  extension_ = visitor;
}

// Passes the call on to the HPQUIC_HTTP_ACK decoder.
void QuicHttpDecoderAdapter::SetDecoderHeaderTableDebugVisitor(
    std::unique_ptr<HpackHeaderTable::DebugVisitorInterface> visitor) {
  GetHpackDecoder()->SetHeaderTableDebugVisitor(std::move(visitor));
}

size_t QuicHttpDecoderAdapter::ProcessInput(const char* data, size_t len) {
  size_t limit = recv_frame_size_limit_;
  frame_decoder_->set_maximum_payload_size(limit);

  size_t total_processed = 0;
  while (len > 0 && spdy_state_ != SPDY_ERROR) {
    // Process one at a time so that we update the adapter's internal
    // state appropriately.
    const size_t processed = ProcessInputFrame(data, len);

    // We had some data, and weren't in an error state, so should have
    // processed/consumed at least one byte of it, even if we then ended up
    // in an error state.
    DCHECK(processed > 0) << "processed=" << processed
                          << "   spdy_state_=" << spdy_state_
                          << "   spdy_framer_error_=" << spdy_framer_error_;

    data += processed;
    len -= processed;
    total_processed += processed;
    if (process_single_input_frame() || processed == 0) {
      break;
    }
  }
  return total_processed;
}

void QuicHttpDecoderAdapter::Reset() {
  ResetInternal();
}

QuicHttpDecoderAdapter::SpdyState QuicHttpDecoderAdapter::state() const {
  return spdy_state_;
}

SpdyFramerError QuicHttpDecoderAdapter::spdy_framer_error() const {
  return spdy_framer_error_;
}

bool QuicHttpDecoderAdapter::probable_http_response() const {
  return latched_probable_http_response_;
}

// ===========================================================================
// Implementations of the methods declared by QuicHttpFrameDecoderListener.

// Called once the common frame header has been decoded for any frame.
// This function is largely based on QuicHttpDecoderAdapter::ValidateFrameHeader
// and some parts of QuicHttpDecoderAdapter::ProcessCommonHeader.
bool QuicHttpDecoderAdapter::OnFrameHeader(const QuicHttpFrameHeader& header) {
  DVLOG(1) << "OnFrameHeader: " << header;
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
    VLOG(1) << "The framer was expecting to receive a " << expected_frame_type_
            << " frame, but instead received an unknown frame of type "
            << header.type;
    SetSpdyErrorAndNotify(SpdyFramerError::SPDY_UNEXPECTED_FRAME);
    return false;
  }
  if (!IsSupportedQuicHttpFrameType(header.type)) {
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
      VLOG(1) << "Unknown control frame type " << header.type
              << " received on invalid stream " << header.stream_id;
      SetSpdyErrorAndNotify(SpdyFramerError::SPDY_INVALID_CONTROL_FRAME);
      return false;
    } else {
      DVLOG(1) << "Ignoring unknown frame type " << header.type;
      return true;
    }
  }

  SpdyFrameType frame_type = ToSpdyFrameType(header.type);
  if (!IsValidHTTP2FrameStreamId(header.stream_id, frame_type)) {
    VLOG(1) << "The framer received an invalid streamID of " << header.stream_id
            << " for a frame of type " << header.type;
    SetSpdyErrorAndNotify(SpdyFramerError::SPDY_INVALID_STREAM_ID);
    return false;
  }

  if (has_expected_frame_type_ && header.type != expected_frame_type_) {
    VLOG(1) << "Expected frame type " << expected_frame_type_ << ", not "
            << header.type;
    SetSpdyErrorAndNotify(SpdyFramerError::SPDY_UNEXPECTED_FRAME);
    return false;
  }

  if (!has_expected_frame_type_ &&
      header.type == QuicHttpFrameType::CONTINUATION) {
    VLOG(1) << "Got CONTINUATION frame when not expected.";
    SetSpdyErrorAndNotify(SpdyFramerError::SPDY_UNEXPECTED_FRAME);
    return false;
  }

  if (header.type == QuicHttpFrameType::DATA) {
    // For some reason SpdyFramer still rejects invalid DATA frame flags.
    uint8_t valid_flags = QuicHttpFrameFlag::QUIC_HTTP_PADDED |
                          QuicHttpFrameFlag::QUIC_HTTP_END_STREAM;
    if (header.HasAnyFlags(~valid_flags)) {
      SetSpdyErrorAndNotify(SpdyFramerError::SPDY_INVALID_DATA_FRAME_FLAGS);
      return false;
    }
  }

  return true;
}

void QuicHttpDecoderAdapter::OnDataStart(const QuicHttpFrameHeader& header) {
  DVLOG(1) << "OnDataStart: " << header;

  if (IsOkToStartFrame(header) && HasRequiredStreamId(header)) {
    frame_header_ = header;
    has_frame_header_ = true;
    visitor()->OnDataFrameHeader(header.stream_id, header.payload_length,
                                 header.IsEndStream());
  }
}

void QuicHttpDecoderAdapter::OnDataPayload(const char* data, size_t len) {
  DVLOG(1) << "OnDataPayload: len=" << len;
  DCHECK(has_frame_header_);
  DCHECK_EQ(frame_header_.type, QuicHttpFrameType::DATA);
  visitor()->OnStreamFrameData(frame_header().stream_id, data, len);
}

void QuicHttpDecoderAdapter::OnDataEnd() {
  DVLOG(1) << "OnDataEnd";
  DCHECK(has_frame_header_);
  DCHECK_EQ(frame_header_.type, QuicHttpFrameType::DATA);
  if (frame_header().IsEndStream()) {
    visitor()->OnStreamEnd(frame_header().stream_id);
  }
  opt_pad_length_ = nullopt;
}

void QuicHttpDecoderAdapter::OnHeadersStart(const QuicHttpFrameHeader& header) {
  DVLOG(1) << "OnHeadersStart: " << header;
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
    visitor()->OnHeaders(header.stream_id, kNotHasPriorityFields,
                         0,      // priority
                         0,      // parent_stream_id
                         false,  // exclusive
                         header.IsEndStream(), header.IsEndHeaders());
    CommonStartHpackBlock();
  }
}

void QuicHttpDecoderAdapter::OnHeadersPriority(
    const QuicHttpPriorityFields& priority) {
  DVLOG(1) << "OnHeadersPriority: " << priority;
  DCHECK(has_frame_header_);
  DCHECK_EQ(frame_type(), QuicHttpFrameType::HEADERS) << frame_header_;
  DCHECK(frame_header_.HasPriority());
  DCHECK(!on_headers_called_);
  on_headers_called_ = true;
  ReportReceiveCompressedFrame(frame_header_);
  visitor()->OnHeaders(frame_header_.stream_id, kHasPriorityFields,
                       priority.weight, priority.stream_dependency,
                       priority.is_exclusive, frame_header_.IsEndStream(),
                       frame_header_.IsEndHeaders());
  CommonStartHpackBlock();
}

void QuicHttpDecoderAdapter::OnHpackFragment(const char* data, size_t len) {
  DVLOG(1) << "OnHpackFragment: len=" << len;
  on_hpack_fragment_called_ = true;
  if (!GetHpackDecoder()->HandleControlFrameHeadersData(data, len)) {
    SetSpdyErrorAndNotify(SpdyFramerError::SPDY_DECOMPRESS_FAILURE);
    return;
  }
}

void QuicHttpDecoderAdapter::OnHeadersEnd() {
  DVLOG(1) << "OnHeadersEnd";
  CommonHpackFragmentEnd();
  opt_pad_length_ = nullopt;
}

void QuicHttpDecoderAdapter::OnPriorityFrame(
    const QuicHttpFrameHeader& header,
    const QuicHttpPriorityFields& priority) {
  DVLOG(1) << "OnPriorityFrame: " << header << "; priority: " << priority;
  if (IsOkToStartFrame(header) && HasRequiredStreamId(header)) {
    visitor()->OnPriority(header.stream_id, priority.stream_dependency,
                          priority.weight, priority.is_exclusive);
  }
}

void QuicHttpDecoderAdapter::OnContinuationStart(
    const QuicHttpFrameHeader& header) {
  DVLOG(1) << "OnContinuationStart: " << header;
  if (IsOkToStartFrame(header) && HasRequiredStreamId(header)) {
    DCHECK(has_hpack_first_frame_header_);
    if (header.stream_id != hpack_first_frame_header_.stream_id) {
      SetSpdyErrorAndNotify(SpdyFramerError::SPDY_UNEXPECTED_FRAME);
      return;
    }
    frame_header_ = header;
    has_frame_header_ = true;
    ReportReceiveCompressedFrame(header);
    visitor()->OnContinuation(header.stream_id, header.IsEndHeaders());
  }
}

void QuicHttpDecoderAdapter::OnContinuationEnd() {
  DVLOG(1) << "OnContinuationEnd";
  CommonHpackFragmentEnd();
}

void QuicHttpDecoderAdapter::OnPadLength(size_t trailing_length) {
  DVLOG(1) << "OnPadLength: " << trailing_length;
  opt_pad_length_ = trailing_length;
  if (frame_header_.type == QuicHttpFrameType::DATA) {
    visitor()->OnStreamPadding(stream_id(), 1);
  } else if (frame_header_.type == QuicHttpFrameType::HEADERS) {
    CHECK_LT(trailing_length, 256u);
  }
}

void QuicHttpDecoderAdapter::OnPadding(const char* padding,
                                       size_t skipped_length) {
  DVLOG(1) << "OnPadding: " << skipped_length;
  if (frame_header_.type == QuicHttpFrameType::DATA) {
    visitor()->OnStreamPadding(stream_id(), skipped_length);
  } else {
    MaybeAnnounceEmptyFirstHpackFragment();
  }
}

void QuicHttpDecoderAdapter::OnRstStream(const QuicHttpFrameHeader& header,
                                         QuicHttpErrorCode http2_error_code) {
  DVLOG(1) << "OnRstStream: " << header << "; code=" << http2_error_code;
  if (IsOkToStartFrame(header) && HasRequiredStreamId(header)) {
    SpdyErrorCode error_code =
        ParseErrorCode(static_cast<uint32_t>(http2_error_code));
    visitor()->OnRstStream(header.stream_id, error_code);
  }
}

void QuicHttpDecoderAdapter::OnSettingsStart(
    const QuicHttpFrameHeader& header) {
  DVLOG(1) << "OnSettingsStart: " << header;
  if (IsOkToStartFrame(header) && HasRequiredStreamIdZero(header)) {
    frame_header_ = header;
    has_frame_header_ = true;
    visitor()->OnSettings();
  }
}

void QuicHttpDecoderAdapter::OnSetting(
    const QuicHttpSettingFields& setting_fields) {
  DVLOG(1) << "OnSetting: " << setting_fields;
  const uint16_t parameter = static_cast<uint16_t>(setting_fields.parameter);
  SpdySettingsIds setting_id;
  if (!ParseSettingsId(parameter, &setting_id)) {
    if (extension_ == nullptr) {
      DVLOG(1) << "Ignoring unknown setting id: " << setting_fields;
    } else {
      extension_->OnSetting(parameter, setting_fields.value);
    }
    return;
  }
  visitor()->OnSetting(setting_id, setting_fields.value);
}

void QuicHttpDecoderAdapter::OnSettingsEnd() {
  DVLOG(1) << "OnSettingsEnd";
  visitor()->OnSettingsEnd();
}

void QuicHttpDecoderAdapter::OnSettingsAck(const QuicHttpFrameHeader& header) {
  DVLOG(1) << "OnSettingsAck: " << header;
  if (IsOkToStartFrame(header) && HasRequiredStreamIdZero(header)) {
    visitor()->OnSettingsAck();
  }
}

void QuicHttpDecoderAdapter::OnPushPromiseStart(
    const QuicHttpFrameHeader& header,
    const QuicHttpPushPromiseFields& promise,
    size_t total_padding_length) {
  DVLOG(1) << "OnPushPromiseStart: " << header << "; promise: " << promise
           << "; total_padding_length: " << total_padding_length;
  if (IsOkToStartFrame(header) && HasRequiredStreamId(header)) {
    if (promise.promised_stream_id == 0) {
      SetSpdyErrorAndNotify(SpdyFramerError::SPDY_INVALID_CONTROL_FRAME);
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

void QuicHttpDecoderAdapter::OnPushPromiseEnd() {
  DVLOG(1) << "OnPushPromiseEnd";
  CommonHpackFragmentEnd();
  opt_pad_length_ = nullopt;
}

void QuicHttpDecoderAdapter::OnPing(const QuicHttpFrameHeader& header,
                                    const QuicHttpPingFields& ping) {
  DVLOG(1) << "OnPing: " << header << "; ping: " << ping;
  if (IsOkToStartFrame(header) && HasRequiredStreamIdZero(header)) {
    visitor()->OnPing(ToSpdyPingId(ping), false);
  }
}

void QuicHttpDecoderAdapter::OnPingAck(const QuicHttpFrameHeader& header,
                                       const QuicHttpPingFields& ping) {
  DVLOG(1) << "OnPingAck: " << header << "; ping: " << ping;
  if (IsOkToStartFrame(header) && HasRequiredStreamIdZero(header)) {
    visitor()->OnPing(ToSpdyPingId(ping), true);
  }
}

void QuicHttpDecoderAdapter::OnGoAwayStart(const QuicHttpFrameHeader& header,
                                           const QuicHttpGoAwayFields& goaway) {
  DVLOG(1) << "OnGoAwayStart: " << header << "; goaway: " << goaway;
  if (IsOkToStartFrame(header) && HasRequiredStreamIdZero(header)) {
    frame_header_ = header;
    has_frame_header_ = true;
    SpdyErrorCode error_code =
        ParseErrorCode(static_cast<uint32_t>(goaway.error_code));
    visitor()->OnGoAway(goaway.last_stream_id, error_code);
  }
}

void QuicHttpDecoderAdapter::OnGoAwayOpaqueData(const char* data, size_t len) {
  DVLOG(1) << "OnGoAwayOpaqueData: len=" << len;
  visitor()->OnGoAwayFrameData(data, len);
}

void QuicHttpDecoderAdapter::OnGoAwayEnd() {
  DVLOG(1) << "OnGoAwayEnd";
  visitor()->OnGoAwayFrameData(nullptr, 0);
}

void QuicHttpDecoderAdapter::OnWindowUpdate(const QuicHttpFrameHeader& header,
                                            uint32_t increment) {
  DVLOG(1) << "OnWindowUpdate: " << header << "; increment=" << increment;
  if (IsOkToStartFrame(header)) {
    visitor()->OnWindowUpdate(header.stream_id, increment);
  }
}

// Per RFC7838, an ALTSVC frame on stream 0 with origin_length == 0, or one on
// a stream other than stream 0 with origin_length != 0 MUST be ignored.  All
// frames are decoded by QuicHttpDecoderAdapter, and it is left to the consumer
// (listener) to implement this behavior.
void QuicHttpDecoderAdapter::OnAltSvcStart(const QuicHttpFrameHeader& header,
                                           size_t origin_length,
                                           size_t value_length) {
  DVLOG(1) << "OnAltSvcStart: " << header
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

void QuicHttpDecoderAdapter::OnAltSvcOriginData(const char* data, size_t len) {
  DVLOG(1) << "OnAltSvcOriginData: len=" << len;
  alt_svc_origin_.append(data, len);
}

// Called when decoding the Alt-Svc-Field-Value of an ALTSVC;
// the field is uninterpreted.
void QuicHttpDecoderAdapter::OnAltSvcValueData(const char* data, size_t len) {
  DVLOG(1) << "OnAltSvcValueData: len=" << len;
  alt_svc_value_.append(data, len);
}

void QuicHttpDecoderAdapter::OnAltSvcEnd() {
  DVLOG(1) << "OnAltSvcEnd: origin.size(): " << alt_svc_origin_.size()
           << "; value.size(): " << alt_svc_value_.size();
  SpdyAltSvcWireFormat::AlternativeServiceVector altsvc_vector;
  if (!SpdyAltSvcWireFormat::ParseHeaderFieldValue(alt_svc_value_,
                                                   &altsvc_vector)) {
    DLOG(ERROR) << "SpdyAltSvcWireFormat::ParseHeaderFieldValue failed.";
    SetSpdyErrorAndNotify(SpdyFramerError::SPDY_INVALID_CONTROL_FRAME);
    return;
  }
  visitor()->OnAltSvc(frame_header_.stream_id, alt_svc_origin_, altsvc_vector);
  // We assume that ALTSVC frames are rare, so get rid of the storage.
  alt_svc_origin_.clear();
  alt_svc_origin_.shrink_to_fit();
  alt_svc_value_.clear();
  alt_svc_value_.shrink_to_fit();
}

// Except for BLOCKED frames, all other unknown frames are either dropped or
// passed to a registered extension.
void QuicHttpDecoderAdapter::OnUnknownStart(const QuicHttpFrameHeader& header) {
  DVLOG(1) << "OnUnknownStart: " << header;
  if (IsOkToStartFrame(header)) {
    if (extension_ != nullptr) {
      const uint8_t type = static_cast<uint8_t>(header.type);
      const uint8_t flags = static_cast<uint8_t>(header.flags);
      handling_extension_payload_ = extension_->OnFrameHeader(
          header.stream_id, header.payload_length, type, flags);
    }
  }
}

void QuicHttpDecoderAdapter::OnUnknownPayload(const char* data, size_t len) {
  if (handling_extension_payload_) {
    extension_->OnFramePayload(data, len);
  } else {
    DVLOG(1) << "OnUnknownPayload: len=" << len;
  }
}

void QuicHttpDecoderAdapter::OnUnknownEnd() {
  DVLOG(1) << "OnUnknownEnd";
  handling_extension_payload_ = false;
}

void QuicHttpDecoderAdapter::OnPaddingTooLong(const QuicHttpFrameHeader& header,
                                              size_t missing_length) {
  DVLOG(1) << "OnPaddingTooLong: " << header
           << "; missing_length: " << missing_length;
  if (header.type == QuicHttpFrameType::DATA) {
    if (header.payload_length == 0) {
      DCHECK_EQ(1u, missing_length);
      SetSpdyErrorAndNotify(SpdyFramerError::SPDY_INVALID_DATA_FRAME_FLAGS);
      return;
    }
    visitor()->OnStreamPadding(header.stream_id, 1);
  }
  SetSpdyErrorAndNotify(SpdyFramerError::SPDY_INVALID_PADDING);
}

void QuicHttpDecoderAdapter::OnFrameSizeError(
    const QuicHttpFrameHeader& header) {
  DVLOG(1) << "OnFrameSizeError: " << header;
  size_t recv_limit = recv_frame_size_limit_;
  if (header.payload_length > recv_limit) {
    SetSpdyErrorAndNotify(SpdyFramerError::SPDY_OVERSIZED_PAYLOAD);
    return;
  }
  if (header.type != QuicHttpFrameType::DATA &&
      header.payload_length > recv_limit) {
    SetSpdyErrorAndNotify(SpdyFramerError::SPDY_CONTROL_PAYLOAD_TOO_LARGE);
    return;
  }
  switch (header.type) {
    case QuicHttpFrameType::GOAWAY:
    case QuicHttpFrameType::ALTSVC:
      SetSpdyErrorAndNotify(SpdyFramerError::SPDY_INVALID_CONTROL_FRAME);
      break;
    default:
      SetSpdyErrorAndNotify(SpdyFramerError::SPDY_INVALID_CONTROL_FRAME_SIZE);
  }
}

// Decodes the input up to the next frame boundary (i.e. at most one frame),
// stopping early if an error is detected.
size_t QuicHttpDecoderAdapter::ProcessInputFrame(const char* data, size_t len) {
  DCHECK_NE(spdy_state_, SpdyState::SPDY_ERROR);
  QuicHttpDecodeBuffer db(data, len);
  QuicHttpDecodeStatus status = frame_decoder_->DecodeFrame(&db);
  if (spdy_state_ != SpdyState::SPDY_ERROR) {
    DetermineSpdyState(status);
  } else {
    VLOG(1) << "ProcessInputFrame spdy_framer_error_="
            << Http2DecoderAdapter::SpdyFramerErrorToString(spdy_framer_error_);
    if (spdy_framer_error_ == SpdyFramerError::SPDY_INVALID_PADDING &&
        has_frame_header_ && frame_type() != QuicHttpFrameType::DATA) {
      // spdy_framer_test checks that all of the available frame payload
      // has been consumed, so do that.
      size_t total = remaining_total_payload();
      if (total <= frame_header().payload_length) {
        size_t avail = db.MinLengthRemaining(total);
        VLOG(1) << "Skipping past " << avail << " bytes, of " << total
                << " total remaining in the frame's payload.";
        db.AdvanceCursor(avail);
      } else {
        SPDY_BUG << "Total remaining (" << total
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
void QuicHttpDecoderAdapter::DetermineSpdyState(QuicHttpDecodeStatus status) {
  DCHECK_EQ(spdy_framer_error_, SpdyFramerError::SPDY_NO_ERROR);
  DCHECK(!HasError()) << spdy_framer_error_;
  switch (status) {
    case QuicHttpDecodeStatus::kDecodeDone:
      DVLOG(1) << "ProcessInputFrame -> QuicHttpDecodeStatus::kDecodeDone";
      ResetBetweenFrames();
      break;
    case QuicHttpDecodeStatus::kDecodeInProgress:
      DVLOG(1)
          << "ProcessInputFrame -> QuicHttpDecodeStatus::kDecodeInProgress";
      if (decoded_frame_header_) {
        if (IsDiscardingPayload()) {
          set_spdy_state(SpdyState::SPDY_IGNORE_REMAINING_PAYLOAD);
        } else if (has_frame_header_ &&
                   frame_type() == QuicHttpFrameType::DATA) {
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
    case QuicHttpDecodeStatus::kDecodeError:
      VLOG(1) << "ProcessInputFrame -> QuicHttpDecodeStatus::kDecodeError";
      if (IsDiscardingPayload()) {
        if (remaining_total_payload() == 0) {
          // Push the QuicHttpFrameDecoder out of state kDiscardPayload now
          // since doing so requires no input.
          QuicHttpDecodeBuffer tmp("", 0);
          QuicHttpDecodeStatus status = frame_decoder_->DecodeFrame(&tmp);
          if (status != QuicHttpDecodeStatus::kDecodeDone) {
            SPDY_BUG << "Expected to be done decoding the frame, not "
                     << status;
            SetSpdyErrorAndNotify(SpdyFramerError::SPDY_INTERNAL_FRAMER_ERROR);
          } else if (spdy_framer_error_ != SpdyFramerError::SPDY_NO_ERROR) {
            SPDY_BUG << "Expected to have no error, not "
                     << Http2DecoderAdapter::SpdyFramerErrorToString(
                            spdy_framer_error_);
          } else {
            ResetBetweenFrames();
          }
        } else {
          set_spdy_state(SpdyState::SPDY_IGNORE_REMAINING_PAYLOAD);
        }
      } else {
        SetSpdyErrorAndNotify(SpdyFramerError::SPDY_INVALID_CONTROL_FRAME);
      }
      break;
  }
}

void QuicHttpDecoderAdapter::ResetBetweenFrames() {
  CorruptFrameHeader(&frame_header_);
  decoded_frame_header_ = false;
  has_frame_header_ = false;
  set_spdy_state(SpdyState::SPDY_READY_FOR_FRAME);
}

// ResetInternal is called from the constructor, and during tests, but not
// otherwise (i.e. not between every frame).
void QuicHttpDecoderAdapter::ResetInternal() {
  set_spdy_state(SpdyState::SPDY_READY_FOR_FRAME);
  spdy_framer_error_ = SpdyFramerError::SPDY_NO_ERROR;

  decoded_frame_header_ = false;
  has_frame_header_ = false;
  on_headers_called_ = false;
  on_hpack_fragment_called_ = false;
  latched_probable_http_response_ = false;
  has_expected_frame_type_ = false;

  CorruptFrameHeader(&frame_header_);
  CorruptFrameHeader(&hpack_first_frame_header_);

  frame_decoder_.reset(new QuicHttpFrameDecoder(this));
  hpack_decoder_ = nullptr;
}

void QuicHttpDecoderAdapter::set_spdy_state(SpdyState v) {
  DVLOG(2) << "set_spdy_state(" << StateToString(v) << ")";
  spdy_state_ = v;
}

void QuicHttpDecoderAdapter::SetSpdyErrorAndNotify(SpdyFramerError error) {
  if (HasError()) {
    DCHECK_EQ(spdy_state_, SpdyState::SPDY_ERROR);
  } else {
    VLOG(2) << "SetSpdyErrorAndNotify("
            << Http2DecoderAdapter::SpdyFramerErrorToString(error) << ")";
    DCHECK_NE(error, SpdyFramerError::SPDY_NO_ERROR);
    spdy_framer_error_ = error;
    set_spdy_state(SpdyState::SPDY_ERROR);
    frame_decoder_->set_listener(&no_op_listener_);
    visitor()->OnError(error);
  }
}

bool QuicHttpDecoderAdapter::HasError() const {
  if (spdy_state_ == SpdyState::SPDY_ERROR) {
    DCHECK_NE(spdy_framer_error(), SpdyFramerError::SPDY_NO_ERROR);
    return true;
  } else {
    DCHECK_EQ(spdy_framer_error(), SpdyFramerError::SPDY_NO_ERROR);
    return false;
  }
}

const QuicHttpFrameHeader& QuicHttpDecoderAdapter::frame_header() const {
  DCHECK(has_frame_header_);
  return frame_header_;
}

uint32_t QuicHttpDecoderAdapter::stream_id() const {
  return frame_header().stream_id;
}

QuicHttpFrameType QuicHttpDecoderAdapter::frame_type() const {
  return frame_header().type;
}

size_t QuicHttpDecoderAdapter::remaining_total_payload() const {
  DCHECK(has_frame_header_);
  size_t remaining = frame_decoder_->remaining_payload();
  if (IsPaddable(frame_type()) && frame_header_.IsPadded()) {
    remaining += frame_decoder_->remaining_padding();
  }
  return remaining;
}

bool QuicHttpDecoderAdapter::IsReadingPaddingLength() {
  bool result = frame_header_.IsPadded() && !opt_pad_length_;
  DVLOG(2) << "QuicHttpDecoderAdapter::IsReadingPaddingLength: " << result;
  return result;
}
bool QuicHttpDecoderAdapter::IsSkippingPadding() {
  bool result = frame_header_.IsPadded() && opt_pad_length_ &&
                frame_decoder_->remaining_payload() == 0 &&
                frame_decoder_->remaining_padding() > 0;
  DVLOG(2) << "QuicHttpDecoderAdapter::IsSkippingPadding: " << result;
  return result;
}
bool QuicHttpDecoderAdapter::IsDiscardingPayload() {
  bool result = decoded_frame_header_ && frame_decoder_->IsDiscardingPayload();
  DVLOG(2) << "QuicHttpDecoderAdapter::IsDiscardingPayload: " << result;
  return result;
}
// Called from OnXyz or OnXyzStart methods to decide whether it is OK to
// handle the callback.
bool QuicHttpDecoderAdapter::IsOkToStartFrame(
    const QuicHttpFrameHeader& header) {
  DVLOG(3) << "IsOkToStartFrame";
  if (HasError()) {
    VLOG(2) << "HasError()";
    return false;
  }
  DCHECK(!has_frame_header_);
  if (has_expected_frame_type_ && header.type != expected_frame_type_) {
    VLOG(1) << "Expected frame type " << expected_frame_type_ << ", not "
            << header.type;
    SetSpdyErrorAndNotify(SpdyFramerError::SPDY_UNEXPECTED_FRAME);
    return false;
  }

  return true;
}

bool QuicHttpDecoderAdapter::HasRequiredStreamId(uint32_t stream_id) {
  DVLOG(3) << "HasRequiredStreamId: " << stream_id;
  if (HasError()) {
    VLOG(2) << "HasError()";
    return false;
  }
  if (stream_id != 0) {
    return true;
  }
  VLOG(1) << "Stream Id is required, but zero provided";
  SetSpdyErrorAndNotify(SpdyFramerError::SPDY_INVALID_STREAM_ID);
  return false;
}

bool QuicHttpDecoderAdapter::HasRequiredStreamId(
    const QuicHttpFrameHeader& header) {
  return HasRequiredStreamId(header.stream_id);
}

bool QuicHttpDecoderAdapter::HasRequiredStreamIdZero(uint32_t stream_id) {
  DVLOG(3) << "HasRequiredStreamIdZero: " << stream_id;
  if (HasError()) {
    VLOG(2) << "HasError()";
    return false;
  }
  if (stream_id == 0) {
    return true;
  }
  VLOG(1) << "Stream Id was not zero, as required: " << stream_id;
  SetSpdyErrorAndNotify(SpdyFramerError::SPDY_INVALID_STREAM_ID);
  return false;
}

bool QuicHttpDecoderAdapter::HasRequiredStreamIdZero(
    const QuicHttpFrameHeader& header) {
  return HasRequiredStreamIdZero(header.stream_id);
}

void QuicHttpDecoderAdapter::ReportReceiveCompressedFrame(
    const QuicHttpFrameHeader& header) {
  if (debug_visitor() != nullptr) {
    size_t total = header.payload_length + QuicHttpFrameHeader::EncodedSize();
    debug_visitor()->OnReceiveCompressedFrame(
        header.stream_id, ToSpdyFrameType(header.type), total);
  }
}

HpackDecoderAdapter* QuicHttpDecoderAdapter::GetHpackDecoder() {
  if (hpack_decoder_ == nullptr) {
    hpack_decoder_ = SpdyMakeUnique<HpackDecoderAdapter>();
  }
  return hpack_decoder_.get();
}

void QuicHttpDecoderAdapter::CommonStartHpackBlock() {
  DVLOG(1) << "CommonStartHpackBlock";
  DCHECK(!has_hpack_first_frame_header_);
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
    SPDY_BUG << "visitor_->OnHeaderFrameStart returned nullptr";
    SetSpdyErrorAndNotify(SpdyFramerError::SPDY_INTERNAL_FRAMER_ERROR);
    return;
  }
  GetHpackDecoder()->HandleControlFrameHeadersStart(handler);
}

// SpdyFramer calls HandleControlFrameHeadersData even if there are zero
// fragment bytes in the first frame, so do the same.
void QuicHttpDecoderAdapter::MaybeAnnounceEmptyFirstHpackFragment() {
  if (!on_hpack_fragment_called_) {
    OnHpackFragment(nullptr, 0);
    DCHECK(on_hpack_fragment_called_);
  }
}

void QuicHttpDecoderAdapter::CommonHpackFragmentEnd() {
  DVLOG(1) << "CommonHpackFragmentEnd: stream_id=" << stream_id();
  if (HasError()) {
    VLOG(1) << "HasError(), returning";
    return;
  }
  DCHECK(has_frame_header_);
  MaybeAnnounceEmptyFirstHpackFragment();
  if (frame_header_.IsEndHeaders()) {
    DCHECK_EQ(has_hpack_first_frame_header_,
              frame_type() == QuicHttpFrameType::CONTINUATION)
        << frame_header();
    has_expected_frame_type_ = false;
    if (GetHpackDecoder()->HandleControlFrameHeadersComplete(nullptr)) {
      visitor()->OnHeaderFrameEnd(stream_id());
    } else {
      SetSpdyErrorAndNotify(SpdyFramerError::SPDY_DECOMPRESS_FAILURE);
      return;
    }
    const QuicHttpFrameHeader& first =
        frame_type() == QuicHttpFrameType::CONTINUATION
            ? hpack_first_frame_header_
            : frame_header_;
    if (first.type == QuicHttpFrameType::HEADERS && first.IsEndStream()) {
      visitor()->OnStreamEnd(first.stream_id);
    }
    has_hpack_first_frame_header_ = false;
    CorruptFrameHeader(&hpack_first_frame_header_);
  } else {
    DCHECK(has_hpack_first_frame_header_);
    has_expected_frame_type_ = true;
    expected_frame_type_ = QuicHttpFrameType::CONTINUATION;
  }
}

}  // namespace net
