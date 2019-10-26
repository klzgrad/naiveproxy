// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/spdy/core/spdy_protocol.h"

#include <limits>
#include <ostream>

#include "net/third_party/quiche/src/spdy/platform/api/spdy_bug_tracker.h"
#include "net/third_party/quiche/src/spdy/platform/api/spdy_ptr_util.h"
#include "net/third_party/quiche/src/spdy/platform/api/spdy_string_utils.h"

namespace spdy {

const char* const kHttp2ConnectionHeaderPrefix =
    "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";

std::ostream& operator<<(std::ostream& out, SpdyKnownSettingsId id) {
  return out << static_cast<SpdySettingsId>(id);
}

std::ostream& operator<<(std::ostream& out, SpdyFrameType frame_type) {
  return out << SerializeFrameType(frame_type);
}

SpdyPriority ClampSpdy3Priority(SpdyPriority priority) {
  static_assert(std::numeric_limits<SpdyPriority>::min() == kV3HighestPriority,
                "The value of given priority shouldn't be smaller than highest "
                "priority. Check this invariant explicitly.");
  if (priority > kV3LowestPriority) {
    SPDY_BUG << "Invalid priority: " << static_cast<int>(priority);
    return kV3LowestPriority;
  }
  return priority;
}

int ClampHttp2Weight(int weight) {
  if (weight < kHttp2MinStreamWeight) {
    SPDY_BUG << "Invalid weight: " << weight;
    return kHttp2MinStreamWeight;
  }
  if (weight > kHttp2MaxStreamWeight) {
    SPDY_BUG << "Invalid weight: " << weight;
    return kHttp2MaxStreamWeight;
  }
  return weight;
}

int Spdy3PriorityToHttp2Weight(SpdyPriority priority) {
  priority = ClampSpdy3Priority(priority);
  const float kSteps = 255.9f / 7.f;
  return static_cast<int>(kSteps * (7.f - priority)) + 1;
}

SpdyPriority Http2WeightToSpdy3Priority(int weight) {
  weight = ClampHttp2Weight(weight);
  const float kSteps = 255.9f / 7.f;
  return static_cast<SpdyPriority>(7.f - (weight - 1) / kSteps);
}

bool IsDefinedFrameType(uint8_t frame_type_field) {
  return frame_type_field <= SerializeFrameType(SpdyFrameType::MAX_FRAME_TYPE);
}

SpdyFrameType ParseFrameType(uint8_t frame_type_field) {
  SPDY_BUG_IF(!IsDefinedFrameType(frame_type_field))
      << "Frame type not defined: " << static_cast<int>(frame_type_field);
  return static_cast<SpdyFrameType>(frame_type_field);
}

uint8_t SerializeFrameType(SpdyFrameType frame_type) {
  return static_cast<uint8_t>(frame_type);
}

bool IsValidHTTP2FrameStreamId(SpdyStreamId current_frame_stream_id,
                               SpdyFrameType frame_type_field) {
  if (current_frame_stream_id == 0) {
    switch (frame_type_field) {
      case SpdyFrameType::DATA:
      case SpdyFrameType::HEADERS:
      case SpdyFrameType::PRIORITY:
      case SpdyFrameType::RST_STREAM:
      case SpdyFrameType::CONTINUATION:
      case SpdyFrameType::PUSH_PROMISE:
        // These frame types must specify a stream
        return false;
      default:
        return true;
    }
  } else {
    switch (frame_type_field) {
      case SpdyFrameType::GOAWAY:
      case SpdyFrameType::SETTINGS:
      case SpdyFrameType::PING:
        // These frame types must not specify a stream
        return false;
      default:
        return true;
    }
  }
}

const char* FrameTypeToString(SpdyFrameType frame_type) {
  switch (frame_type) {
    case SpdyFrameType::DATA:
      return "DATA";
    case SpdyFrameType::RST_STREAM:
      return "RST_STREAM";
    case SpdyFrameType::SETTINGS:
      return "SETTINGS";
    case SpdyFrameType::PING:
      return "PING";
    case SpdyFrameType::GOAWAY:
      return "GOAWAY";
    case SpdyFrameType::HEADERS:
      return "HEADERS";
    case SpdyFrameType::WINDOW_UPDATE:
      return "WINDOW_UPDATE";
    case SpdyFrameType::PUSH_PROMISE:
      return "PUSH_PROMISE";
    case SpdyFrameType::CONTINUATION:
      return "CONTINUATION";
    case SpdyFrameType::PRIORITY:
      return "PRIORITY";
    case SpdyFrameType::ALTSVC:
      return "ALTSVC";
    case SpdyFrameType::EXTENSION:
      return "EXTENSION (unspecified)";
  }
  return "UNKNOWN_FRAME_TYPE";
}

bool ParseSettingsId(SpdySettingsId wire_setting_id,
                     SpdyKnownSettingsId* setting_id) {
  if (wire_setting_id != SETTINGS_EXPERIMENT_SCHEDULER &&
      (wire_setting_id < SETTINGS_MIN || wire_setting_id > SETTINGS_MAX)) {
    return false;
  }

  *setting_id = static_cast<SpdyKnownSettingsId>(wire_setting_id);
  // This switch ensures that the casted value is valid. The default case is
  // explicitly omitted to have compile-time guarantees that new additions to
  // |SpdyKnownSettingsId| must also be handled here.
  switch (*setting_id) {
    case SETTINGS_HEADER_TABLE_SIZE:
    case SETTINGS_ENABLE_PUSH:
    case SETTINGS_MAX_CONCURRENT_STREAMS:
    case SETTINGS_INITIAL_WINDOW_SIZE:
    case SETTINGS_MAX_FRAME_SIZE:
    case SETTINGS_MAX_HEADER_LIST_SIZE:
    case SETTINGS_ENABLE_CONNECT_PROTOCOL:
    case SETTINGS_EXPERIMENT_SCHEDULER:
      // FALLTHROUGH_INTENDED
      return true;
  }
  return false;
}

std::string SettingsIdToString(SpdySettingsId id) {
  SpdyKnownSettingsId known_id;
  if (!ParseSettingsId(id, &known_id)) {
    return SpdyStrCat("SETTINGS_UNKNOWN_",
                      SpdyHexEncodeUInt32AndTrim(uint32_t{id}));
  }

  switch (known_id) {
    case SETTINGS_HEADER_TABLE_SIZE:
      return "SETTINGS_HEADER_TABLE_SIZE";
    case SETTINGS_ENABLE_PUSH:
      return "SETTINGS_ENABLE_PUSH";
    case SETTINGS_MAX_CONCURRENT_STREAMS:
      return "SETTINGS_MAX_CONCURRENT_STREAMS";
    case SETTINGS_INITIAL_WINDOW_SIZE:
      return "SETTINGS_INITIAL_WINDOW_SIZE";
    case SETTINGS_MAX_FRAME_SIZE:
      return "SETTINGS_MAX_FRAME_SIZE";
    case SETTINGS_MAX_HEADER_LIST_SIZE:
      return "SETTINGS_MAX_HEADER_LIST_SIZE";
    case SETTINGS_ENABLE_CONNECT_PROTOCOL:
      return "SETTINGS_ENABLE_CONNECT_PROTOCOL";
    case SETTINGS_EXPERIMENT_SCHEDULER:
      return "SETTINGS_EXPERIMENT_SCHEDULER";
  }

  return SpdyStrCat("SETTINGS_UNKNOWN_",
                    SpdyHexEncodeUInt32AndTrim(uint32_t{id}));
}

SpdyErrorCode ParseErrorCode(uint32_t wire_error_code) {
  if (wire_error_code > ERROR_CODE_MAX) {
    return ERROR_CODE_INTERNAL_ERROR;
  }

  return static_cast<SpdyErrorCode>(wire_error_code);
}

const char* ErrorCodeToString(SpdyErrorCode error_code) {
  switch (error_code) {
    case ERROR_CODE_NO_ERROR:
      return "NO_ERROR";
    case ERROR_CODE_PROTOCOL_ERROR:
      return "PROTOCOL_ERROR";
    case ERROR_CODE_INTERNAL_ERROR:
      return "INTERNAL_ERROR";
    case ERROR_CODE_FLOW_CONTROL_ERROR:
      return "FLOW_CONTROL_ERROR";
    case ERROR_CODE_SETTINGS_TIMEOUT:
      return "SETTINGS_TIMEOUT";
    case ERROR_CODE_STREAM_CLOSED:
      return "STREAM_CLOSED";
    case ERROR_CODE_FRAME_SIZE_ERROR:
      return "FRAME_SIZE_ERROR";
    case ERROR_CODE_REFUSED_STREAM:
      return "REFUSED_STREAM";
    case ERROR_CODE_CANCEL:
      return "CANCEL";
    case ERROR_CODE_COMPRESSION_ERROR:
      return "COMPRESSION_ERROR";
    case ERROR_CODE_CONNECT_ERROR:
      return "CONNECT_ERROR";
    case ERROR_CODE_ENHANCE_YOUR_CALM:
      return "ENHANCE_YOUR_CALM";
    case ERROR_CODE_INADEQUATE_SECURITY:
      return "INADEQUATE_SECURITY";
    case ERROR_CODE_HTTP_1_1_REQUIRED:
      return "HTTP_1_1_REQUIRED";
  }
  return "UNKNOWN_ERROR_CODE";
}

const char* WriteSchedulerTypeToString(WriteSchedulerType type) {
  switch (type) {
    case WriteSchedulerType::LIFO:
      return "LIFO";
    case WriteSchedulerType::SPDY:
      return "SPDY";
    case WriteSchedulerType::HTTP2:
      return "HTTP2";
    case WriteSchedulerType::FIFO:
      return "FIFO";
  }
  return "UNKNOWN";
}

size_t GetNumberRequiredContinuationFrames(size_t size) {
  DCHECK_GT(size, kHttp2MaxControlFrameSendSize);
  size_t overflow = size - kHttp2MaxControlFrameSendSize;
  int payload_size =
      kHttp2MaxControlFrameSendSize - kContinuationFrameMinimumSize;
  // This is ceiling(overflow/payload_size) using integer arithmetics.
  return (overflow - 1) / payload_size + 1;
}

const char* const kHttp2Npn = "h2";

const char* const kHttp2AuthorityHeader = ":authority";
const char* const kHttp2MethodHeader = ":method";
const char* const kHttp2PathHeader = ":path";
const char* const kHttp2SchemeHeader = ":scheme";
const char* const kHttp2ProtocolHeader = ":protocol";

const char* const kHttp2StatusHeader = ":status";

bool SpdyFrameIR::fin() const {
  return false;
}

int SpdyFrameIR::flow_control_window_consumed() const {
  return 0;
}

bool SpdyFrameWithFinIR::fin() const {
  return fin_;
}

SpdyFrameWithHeaderBlockIR::SpdyFrameWithHeaderBlockIR(
    SpdyStreamId stream_id,
    SpdyHeaderBlock header_block)
    : SpdyFrameWithFinIR(stream_id), header_block_(std::move(header_block)) {}

SpdyFrameWithHeaderBlockIR::~SpdyFrameWithHeaderBlockIR() = default;

SpdyDataIR::SpdyDataIR(SpdyStreamId stream_id, SpdyStringPiece data)
    : SpdyFrameWithFinIR(stream_id),
      data_(nullptr),
      data_len_(0),
      padded_(false),
      padding_payload_len_(0) {
  SetDataDeep(data);
}

SpdyDataIR::SpdyDataIR(SpdyStreamId stream_id, const char* data)
    : SpdyDataIR(stream_id, SpdyStringPiece(data)) {}

SpdyDataIR::SpdyDataIR(SpdyStreamId stream_id, std::string data)
    : SpdyFrameWithFinIR(stream_id),
      data_store_(SpdyMakeUnique<std::string>(std::move(data))),
      data_(data_store_->data()),
      data_len_(data_store_->size()),
      padded_(false),
      padding_payload_len_(0) {}

SpdyDataIR::SpdyDataIR(SpdyStreamId stream_id)
    : SpdyFrameWithFinIR(stream_id),
      data_(nullptr),
      data_len_(0),
      padded_(false),
      padding_payload_len_(0) {}

SpdyDataIR::~SpdyDataIR() = default;

void SpdyDataIR::Visit(SpdyFrameVisitor* visitor) const {
  return visitor->VisitData(*this);
}

SpdyFrameType SpdyDataIR::frame_type() const {
  return SpdyFrameType::DATA;
}

int SpdyDataIR::flow_control_window_consumed() const {
  return padded_ ? 1 + padding_payload_len_ + data_len_ : data_len_;
}

size_t SpdyDataIR::size() const {
  return kFrameHeaderSize +
         (padded() ? 1 + padding_payload_len() + data_len() : data_len());
}

SpdyRstStreamIR::SpdyRstStreamIR(SpdyStreamId stream_id,
                                 SpdyErrorCode error_code)
    : SpdyFrameIR(stream_id) {
  set_error_code(error_code);
}

SpdyRstStreamIR::~SpdyRstStreamIR() = default;

void SpdyRstStreamIR::Visit(SpdyFrameVisitor* visitor) const {
  return visitor->VisitRstStream(*this);
}

SpdyFrameType SpdyRstStreamIR::frame_type() const {
  return SpdyFrameType::RST_STREAM;
}

size_t SpdyRstStreamIR::size() const {
  return kRstStreamFrameSize;
}

SpdySettingsIR::SpdySettingsIR() : is_ack_(false) {}

SpdySettingsIR::~SpdySettingsIR() = default;

void SpdySettingsIR::Visit(SpdyFrameVisitor* visitor) const {
  return visitor->VisitSettings(*this);
}

SpdyFrameType SpdySettingsIR::frame_type() const {
  return SpdyFrameType::SETTINGS;
}

size_t SpdySettingsIR::size() const {
  return kFrameHeaderSize + values_.size() * kSettingsOneSettingSize;
}

void SpdyPingIR::Visit(SpdyFrameVisitor* visitor) const {
  return visitor->VisitPing(*this);
}

SpdyFrameType SpdyPingIR::frame_type() const {
  return SpdyFrameType::PING;
}

size_t SpdyPingIR::size() const {
  return kPingFrameSize;
}

SpdyGoAwayIR::SpdyGoAwayIR(SpdyStreamId last_good_stream_id,
                           SpdyErrorCode error_code,
                           SpdyStringPiece description)
    : description_(description) {
  set_last_good_stream_id(last_good_stream_id);
  set_error_code(error_code);
}

SpdyGoAwayIR::SpdyGoAwayIR(SpdyStreamId last_good_stream_id,
                           SpdyErrorCode error_code,
                           const char* description)
    : SpdyGoAwayIR(last_good_stream_id,
                   error_code,
                   SpdyStringPiece(description)) {}

SpdyGoAwayIR::SpdyGoAwayIR(SpdyStreamId last_good_stream_id,
                           SpdyErrorCode error_code,
                           std::string description)
    : description_store_(std::move(description)),
      description_(description_store_) {
  set_last_good_stream_id(last_good_stream_id);
  set_error_code(error_code);
}

SpdyGoAwayIR::~SpdyGoAwayIR() = default;

void SpdyGoAwayIR::Visit(SpdyFrameVisitor* visitor) const {
  return visitor->VisitGoAway(*this);
}

SpdyFrameType SpdyGoAwayIR::frame_type() const {
  return SpdyFrameType::GOAWAY;
}

size_t SpdyGoAwayIR::size() const {
  return kGoawayFrameMinimumSize + description_.size();
}

SpdyContinuationIR::SpdyContinuationIR(SpdyStreamId stream_id)
    : SpdyFrameIR(stream_id), end_headers_(false) {
  encoding_ = SpdyMakeUnique<std::string>();
}

SpdyContinuationIR::~SpdyContinuationIR() = default;

void SpdyContinuationIR::Visit(SpdyFrameVisitor* visitor) const {
  return visitor->VisitContinuation(*this);
}

SpdyFrameType SpdyContinuationIR::frame_type() const {
  return SpdyFrameType::CONTINUATION;
}

size_t SpdyContinuationIR::size() const {
  // We don't need to get the size of CONTINUATION frame directly. It is
  // calculated in HEADERS or PUSH_PROMISE frame.
  SPDY_DLOG(WARNING) << "Shouldn't not call size() for CONTINUATION frame.";
  return 0;
}

void SpdyHeadersIR::Visit(SpdyFrameVisitor* visitor) const {
  return visitor->VisitHeaders(*this);
}

SpdyFrameType SpdyHeadersIR::frame_type() const {
  return SpdyFrameType::HEADERS;
}

size_t SpdyHeadersIR::size() const {
  size_t size = kHeadersFrameMinimumSize;

  if (padded_) {
    // Padding field length.
    size += 1;
    size += padding_payload_len_;
  }

  if (has_priority_) {
    size += 5;
  }

  // Assume no hpack encoding is applied.
  size += header_block().TotalBytesUsed() +
          header_block().size() * kPerHeaderHpackOverhead;
  if (size > kHttp2MaxControlFrameSendSize) {
    size += GetNumberRequiredContinuationFrames(size) *
            kContinuationFrameMinimumSize;
  }
  return size;
}

void SpdyWindowUpdateIR::Visit(SpdyFrameVisitor* visitor) const {
  return visitor->VisitWindowUpdate(*this);
}

SpdyFrameType SpdyWindowUpdateIR::frame_type() const {
  return SpdyFrameType::WINDOW_UPDATE;
}

size_t SpdyWindowUpdateIR::size() const {
  return kWindowUpdateFrameSize;
}

void SpdyPushPromiseIR::Visit(SpdyFrameVisitor* visitor) const {
  return visitor->VisitPushPromise(*this);
}

SpdyFrameType SpdyPushPromiseIR::frame_type() const {
  return SpdyFrameType::PUSH_PROMISE;
}

size_t SpdyPushPromiseIR::size() const {
  size_t size = kPushPromiseFrameMinimumSize;

  if (padded_) {
    // Padding length field.
    size += 1;
    size += padding_payload_len_;
  }

  size += header_block().TotalBytesUsed();
  if (size > kHttp2MaxControlFrameSendSize) {
    size += GetNumberRequiredContinuationFrames(size) *
            kContinuationFrameMinimumSize;
  }
  return size;
}

SpdyAltSvcIR::SpdyAltSvcIR(SpdyStreamId stream_id) : SpdyFrameIR(stream_id) {}

SpdyAltSvcIR::~SpdyAltSvcIR() = default;

void SpdyAltSvcIR::Visit(SpdyFrameVisitor* visitor) const {
  return visitor->VisitAltSvc(*this);
}

SpdyFrameType SpdyAltSvcIR::frame_type() const {
  return SpdyFrameType::ALTSVC;
}

size_t SpdyAltSvcIR::size() const {
  size_t size = kGetAltSvcFrameMinimumSize;
  size += origin_.length();
  // TODO(yasong): estimates the size without serializing the vector.
  std::string str =
      SpdyAltSvcWireFormat::SerializeHeaderFieldValue(altsvc_vector_);
  size += str.size();
  return size;
}

void SpdyPriorityIR::Visit(SpdyFrameVisitor* visitor) const {
  return visitor->VisitPriority(*this);
}

SpdyFrameType SpdyPriorityIR::frame_type() const {
  return SpdyFrameType::PRIORITY;
}

size_t SpdyPriorityIR::size() const {
  return kPriorityFrameSize;
}

void SpdyUnknownIR::Visit(SpdyFrameVisitor* visitor) const {
  return visitor->VisitUnknown(*this);
}

SpdyFrameType SpdyUnknownIR::frame_type() const {
  return static_cast<SpdyFrameType>(type());
}

size_t SpdyUnknownIR::size() const {
  return kFrameHeaderSize + payload_.size();
}

int SpdyUnknownIR::flow_control_window_consumed() const {
  if (frame_type() == SpdyFrameType::DATA) {
    return payload_.size();
  } else {
    return 0;
  }
}

// Wire size of pad length field.
const size_t kPadLengthFieldSize = 1;

size_t GetHeaderFrameSizeSansBlock(const SpdyHeadersIR& header_ir) {
  size_t min_size = kFrameHeaderSize;
  if (header_ir.padded()) {
    min_size += kPadLengthFieldSize;
    min_size += header_ir.padding_payload_len();
  }
  if (header_ir.has_priority()) {
    min_size += 5;
  }
  return min_size;
}

size_t GetPushPromiseFrameSizeSansBlock(
    const SpdyPushPromiseIR& push_promise_ir) {
  size_t min_size = kPushPromiseFrameMinimumSize;
  if (push_promise_ir.padded()) {
    min_size += kPadLengthFieldSize;
    min_size += push_promise_ir.padding_payload_len();
  }
  return min_size;
}

}  // namespace spdy
