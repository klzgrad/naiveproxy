#include "quiche/http2/core/http2_trace_logging.h"

#include <cstdint>
#include <memory>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/spdy/core/http2_header_block.h"
#include "quiche/spdy/core/spdy_protocol.h"

// Convenience macros for printing function arguments in log lines in the
// format arg_name=value.
#define FORMAT_ARG(arg) " " #arg "=" << arg
#define FORMAT_INT_ARG(arg) " " #arg "=" << static_cast<int>(arg)

// Convenience macros for printing Spdy*IR attributes in log lines in the
// format attrib_name=value.
#define FORMAT_ATTR(ir, attrib) " " #attrib "=" << ir.attrib()
#define FORMAT_INT_ATTR(ir, attrib) \
  " " #attrib "=" << static_cast<int>(ir.attrib())

namespace {

// Logs a container, using a user-provided object to log each individual item.
template <typename T, typename ItemLogger>
struct ContainerLogger {
  explicit ContainerLogger(const T& c, ItemLogger l)
      : container(c), item_logger(l) {}

  friend std::ostream& operator<<(std::ostream& out,
                                  const ContainerLogger& logger) {
    out << "[";
    auto begin = logger.container.begin();
    for (auto it = begin; it != logger.container.end(); ++it) {
      if (it != begin) {
        out << ", ";
      }
      logger.item_logger.Log(out, *it);
    }
    out << "]";
    return out;
  }
  const T& container;
  ItemLogger item_logger;
};

// Returns a ContainerLogger that will log |container| using |item_logger|.
template <typename T, typename ItemLogger>
auto LogContainer(const T& container, ItemLogger item_logger)
    -> decltype(ContainerLogger<T, ItemLogger>(container, item_logger)) {
  return ContainerLogger<T, ItemLogger>(container, item_logger);
}

}  // anonymous namespace

#define FORMAT_HEADER_BLOCK(ir) \
  " header_block=" << LogContainer(ir.header_block(), LogHeaderBlockEntry())

namespace http2 {

using spdy::Http2HeaderBlock;
using spdy::SettingsMap;
using spdy::SpdyAltSvcIR;
using spdy::SpdyContinuationIR;
using spdy::SpdyDataIR;
using spdy::SpdyGoAwayIR;
using spdy::SpdyHeadersIR;
using spdy::SpdyPingIR;
using spdy::SpdyPriorityIR;
using spdy::SpdyPushPromiseIR;
using spdy::SpdyRstStreamIR;
using spdy::SpdySettingsIR;
using spdy::SpdyStreamId;
using spdy::SpdyUnknownIR;
using spdy::SpdyWindowUpdateIR;

namespace {

// Defines how elements of Http2HeaderBlocks are logged.
struct LogHeaderBlockEntry {
  void Log(std::ostream& out,
           const Http2HeaderBlock::value_type& entry) const {  // NOLINT
    out << "\"" << entry.first << "\": \"" << entry.second << "\"";
  }
};

// Defines how elements of SettingsMap are logged.
struct LogSettingsEntry {
  void Log(std::ostream& out,
           const SettingsMap::value_type& entry) const {  // NOLINT
    out << spdy::SettingsIdToString(entry.first) << ": " << entry.second;
  }
};

// Defines how elements of AlternativeServiceVector are logged.
struct LogAlternativeService {
  void Log(std::ostream& out,
           const spdy::SpdyAltSvcWireFormat::AlternativeService& altsvc)
      const {  // NOLINT
    out << "{"
        << "protocol_id=" << altsvc.protocol_id << " host=" << altsvc.host
        << " port=" << altsvc.port
        << " max_age_seconds=" << altsvc.max_age_seconds << " version=";
    for (auto v : altsvc.version) {
      out << v << ",";
    }
    out << "}";
  }
};

}  // anonymous namespace

Http2TraceLogger::Http2TraceLogger(SpdyFramerVisitorInterface* parent,
                                   absl::string_view perspective,
                                   quiche::MultiUseCallback<bool()> is_enabled,
                                   const void* connection_id)
    : wrapped_(parent),
      perspective_(perspective),
      is_enabled_(std::move(is_enabled)),
      connection_id_(connection_id) {}

Http2TraceLogger::~Http2TraceLogger() {
  if (recording_headers_handler_ != nullptr &&
      !recording_headers_handler_->decoded_block().empty()) {
    HTTP2_TRACE_LOG(perspective_, is_enabled_)
        << "connection_id=" << connection_id_
        << " Received headers that were never logged! keys/values:"
        << recording_headers_handler_->decoded_block().DebugString();
  }
}

void Http2TraceLogger::OnError(Http2DecoderAdapter::SpdyFramerError error,
                               std::string detailed_error) {
  HTTP2_TRACE_LOG(perspective_, is_enabled_)
      << "OnError:" << FORMAT_ARG(connection_id_)
      << ", error=" << Http2DecoderAdapter::SpdyFramerErrorToString(error);
  wrapped_->OnError(error, detailed_error);
}

void Http2TraceLogger::OnCommonHeader(SpdyStreamId stream_id, size_t length,
                                      uint8_t type, uint8_t flags) {
  HTTP2_TRACE_LOG(perspective_, is_enabled_)
      << "OnCommonHeader:" << FORMAT_ARG(connection_id_)
      << FORMAT_ARG(stream_id) << FORMAT_ARG(length) << FORMAT_INT_ARG(type)
      << FORMAT_INT_ARG(flags);
  wrapped_->OnCommonHeader(stream_id, length, type, flags);
}

void Http2TraceLogger::OnDataFrameHeader(SpdyStreamId stream_id, size_t length,
                                         bool fin) {
  HTTP2_TRACE_LOG(perspective_, is_enabled_)
      << "OnDataFrameHeader:" << FORMAT_ARG(connection_id_)
      << FORMAT_ARG(stream_id) << FORMAT_ARG(length) << FORMAT_ARG(fin);
  wrapped_->OnDataFrameHeader(stream_id, length, fin);
}

void Http2TraceLogger::OnStreamFrameData(SpdyStreamId stream_id,
                                         const char* data, size_t len) {
  HTTP2_TRACE_LOG(perspective_, is_enabled_)
      << "OnStreamFrameData:" << FORMAT_ARG(connection_id_)
      << FORMAT_ARG(stream_id) << FORMAT_ARG(len);
  wrapped_->OnStreamFrameData(stream_id, data, len);
}

void Http2TraceLogger::OnStreamEnd(SpdyStreamId stream_id) {
  HTTP2_TRACE_LOG(perspective_, is_enabled_)
      << "OnStreamEnd:" << FORMAT_ARG(connection_id_) << FORMAT_ARG(stream_id);
  wrapped_->OnStreamEnd(stream_id);
}

void Http2TraceLogger::OnStreamPadLength(SpdyStreamId stream_id, size_t value) {
  HTTP2_TRACE_LOG(perspective_, is_enabled_)
      << "OnStreamPadLength:" << FORMAT_ARG(connection_id_)
      << FORMAT_ARG(stream_id) << FORMAT_ARG(value);
  wrapped_->OnStreamPadLength(stream_id, value);
}

void Http2TraceLogger::OnStreamPadding(SpdyStreamId stream_id, size_t len) {
  HTTP2_TRACE_LOG(perspective_, is_enabled_)
      << "OnStreamPadding:" << FORMAT_ARG(connection_id_)
      << FORMAT_ARG(stream_id) << FORMAT_ARG(len);
  wrapped_->OnStreamPadding(stream_id, len);
}

spdy::SpdyHeadersHandlerInterface* Http2TraceLogger::OnHeaderFrameStart(
    SpdyStreamId stream_id) {
  HTTP2_TRACE_LOG(perspective_, is_enabled_)
      << "OnHeaderFrameStart:" << FORMAT_ARG(connection_id_)
      << FORMAT_ARG(stream_id);
  spdy::SpdyHeadersHandlerInterface* result =
      wrapped_->OnHeaderFrameStart(stream_id);
  if (is_enabled_()) {
    recording_headers_handler_ =
        std::make_unique<spdy::RecordingHeadersHandler>(result);
    result = recording_headers_handler_.get();
  } else {
    recording_headers_handler_ = nullptr;
  }
  return result;
}

void Http2TraceLogger::OnHeaderFrameEnd(SpdyStreamId stream_id) {
  HTTP2_TRACE_LOG(perspective_, is_enabled_)
      << "OnHeaderFrameEnd:" << FORMAT_ARG(connection_id_)
      << FORMAT_ARG(stream_id);
  LogReceivedHeaders();
  wrapped_->OnHeaderFrameEnd(stream_id);
  recording_headers_handler_ = nullptr;
}

void Http2TraceLogger::OnRstStream(SpdyStreamId stream_id,
                                   SpdyErrorCode error_code) {
  HTTP2_TRACE_LOG(perspective_, is_enabled_)
      << "OnRstStream:" << FORMAT_ARG(connection_id_) << FORMAT_ARG(stream_id)
      << " error_code=" << spdy::ErrorCodeToString(error_code);
  wrapped_->OnRstStream(stream_id, error_code);
}

void Http2TraceLogger::OnSettings() { wrapped_->OnSettings(); }

void Http2TraceLogger::OnSetting(SpdySettingsId id, uint32_t value) {
  HTTP2_TRACE_LOG(perspective_, is_enabled_)
      << "OnSetting:" << FORMAT_ARG(connection_id_)
      << " id=" << spdy::SettingsIdToString(id) << FORMAT_ARG(value);
  wrapped_->OnSetting(id, value);
}

void Http2TraceLogger::OnSettingsEnd() {
  HTTP2_TRACE_LOG(perspective_, is_enabled_)
      << "OnSettingsEnd:" << FORMAT_ARG(connection_id_);
  wrapped_->OnSettingsEnd();
}

void Http2TraceLogger::OnSettingsAck() {
  HTTP2_TRACE_LOG(perspective_, is_enabled_)
      << "OnSettingsAck:" << FORMAT_ARG(connection_id_);
  wrapped_->OnSettingsAck();
}

void Http2TraceLogger::OnPing(SpdyPingId unique_id, bool is_ack) {
  HTTP2_TRACE_LOG(perspective_, is_enabled_)
      << "OnPing:" << FORMAT_ARG(connection_id_) << FORMAT_ARG(unique_id)
      << FORMAT_ARG(is_ack);
  wrapped_->OnPing(unique_id, is_ack);
}

void Http2TraceLogger::OnGoAway(SpdyStreamId last_accepted_stream_id,
                                SpdyErrorCode error_code) {
  HTTP2_TRACE_LOG(perspective_, is_enabled_)
      << "OnGoAway:" << FORMAT_ARG(connection_id_)
      << FORMAT_ARG(last_accepted_stream_id)
      << " error_code=" << spdy::ErrorCodeToString(error_code);
  wrapped_->OnGoAway(last_accepted_stream_id, error_code);
}

bool Http2TraceLogger::OnGoAwayFrameData(const char* goaway_data, size_t len) {
  return wrapped_->OnGoAwayFrameData(goaway_data, len);
}

void Http2TraceLogger::OnHeaders(SpdyStreamId stream_id, size_t payload_length,
                                 bool has_priority, int weight,
                                 SpdyStreamId parent_stream_id, bool exclusive,
                                 bool fin, bool end) {
  HTTP2_TRACE_LOG(perspective_, is_enabled_)
      << "OnHeaders:" << FORMAT_ARG(connection_id_) << FORMAT_ARG(stream_id)
      << FORMAT_ARG(payload_length) << FORMAT_ARG(has_priority)
      << FORMAT_INT_ARG(weight) << FORMAT_ARG(parent_stream_id)
      << FORMAT_ARG(exclusive) << FORMAT_ARG(fin) << FORMAT_ARG(end);
  wrapped_->OnHeaders(stream_id, payload_length, has_priority, weight,
                      parent_stream_id, exclusive, fin, end);
}

void Http2TraceLogger::OnWindowUpdate(SpdyStreamId stream_id,
                                      int delta_window_size) {
  HTTP2_TRACE_LOG(perspective_, is_enabled_)
      << "OnWindowUpdate:" << FORMAT_ARG(connection_id_)
      << FORMAT_ARG(stream_id) << FORMAT_ARG(delta_window_size);
  wrapped_->OnWindowUpdate(stream_id, delta_window_size);
}

void Http2TraceLogger::OnPushPromise(SpdyStreamId original_stream_id,
                                     SpdyStreamId promised_stream_id,
                                     bool end) {
  HTTP2_TRACE_LOG(perspective_, is_enabled_)
      << "OnPushPromise:" << FORMAT_ARG(connection_id_)
      << FORMAT_ARG(original_stream_id) << FORMAT_ARG(promised_stream_id)
      << FORMAT_ARG(end);
  wrapped_->OnPushPromise(original_stream_id, promised_stream_id, end);
}

void Http2TraceLogger::OnContinuation(SpdyStreamId stream_id,
                                      size_t payload_length, bool end) {
  HTTP2_TRACE_LOG(perspective_, is_enabled_)
      << "OnContinuation:" << FORMAT_ARG(connection_id_)
      << FORMAT_ARG(stream_id) << FORMAT_ARG(payload_length) << FORMAT_ARG(end);
  wrapped_->OnContinuation(stream_id, payload_length, end);
}

void Http2TraceLogger::OnAltSvc(
    SpdyStreamId stream_id, absl::string_view origin,
    const SpdyAltSvcWireFormat::AlternativeServiceVector& altsvc_vector) {
  HTTP2_TRACE_LOG(perspective_, is_enabled_)
      << "OnAltSvc:" << FORMAT_ARG(connection_id_) << FORMAT_ARG(stream_id)
      << FORMAT_ARG(origin) << " altsvc_vector="
      << LogContainer(altsvc_vector, LogAlternativeService());
  wrapped_->OnAltSvc(stream_id, origin, altsvc_vector);
}

void Http2TraceLogger::OnPriority(SpdyStreamId stream_id,
                                  SpdyStreamId parent_stream_id, int weight,
                                  bool exclusive) {
  HTTP2_TRACE_LOG(perspective_, is_enabled_)
      << "OnPriority:" << FORMAT_ARG(connection_id_) << FORMAT_ARG(stream_id)
      << FORMAT_ARG(parent_stream_id) << FORMAT_INT_ARG(weight)
      << FORMAT_ARG(exclusive);
  wrapped_->OnPriority(stream_id, parent_stream_id, weight, exclusive);
}

void Http2TraceLogger::OnPriorityUpdate(
    SpdyStreamId prioritized_stream_id,
    absl::string_view priority_field_value) {
  HTTP2_TRACE_LOG(perspective_, is_enabled_)
      << "OnPriorityUpdate:" << FORMAT_ARG(connection_id_)
      << FORMAT_ARG(prioritized_stream_id) << FORMAT_ARG(priority_field_value);
  wrapped_->OnPriorityUpdate(prioritized_stream_id, priority_field_value);
}

bool Http2TraceLogger::OnUnknownFrame(SpdyStreamId stream_id,
                                      uint8_t frame_type) {
  HTTP2_TRACE_LOG(perspective_, is_enabled_)
      << "OnUnknownFrame:" << FORMAT_ARG(connection_id_)
      << FORMAT_ARG(stream_id) << FORMAT_INT_ARG(frame_type);
  return wrapped_->OnUnknownFrame(stream_id, frame_type);
}

void Http2TraceLogger::OnUnknownFrameStart(spdy::SpdyStreamId stream_id,
                                           size_t length, uint8_t type,
                                           uint8_t flags) {
  HTTP2_TRACE_LOG(perspective_, is_enabled_)
      << "OnUnknownFrameStart:" << FORMAT_ARG(connection_id_)
      << FORMAT_ARG(stream_id) << FORMAT_ARG(length) << FORMAT_INT_ARG(type)
      << FORMAT_INT_ARG(flags);
  wrapped_->OnUnknownFrameStart(stream_id, length, type, flags);
}

void Http2TraceLogger::OnUnknownFramePayload(spdy::SpdyStreamId stream_id,
                                             absl::string_view payload) {
  HTTP2_TRACE_LOG(perspective_, is_enabled_)
      << "OnUnknownFramePayload:" << FORMAT_ARG(connection_id_)
      << FORMAT_ARG(stream_id) << " length=" << payload.size();
  wrapped_->OnUnknownFramePayload(stream_id, payload);
}

void Http2TraceLogger::LogReceivedHeaders() const {
  if (recording_headers_handler_ == nullptr) {
    // Trace logging was not enabled when the start of the header block was
    // received.
    return;
  }
  HTTP2_TRACE_LOG(perspective_, is_enabled_)
      << "Received headers;" << FORMAT_ARG(connection_id_) << " keys/values:"
      << recording_headers_handler_->decoded_block().DebugString()
      << " compressed_bytes="
      << recording_headers_handler_->compressed_header_bytes()
      << " uncompressed_bytes="
      << recording_headers_handler_->uncompressed_header_bytes();
}

void Http2FrameLogger::VisitRstStream(const SpdyRstStreamIR& rst_stream) {
  HTTP2_TRACE_LOG(perspective_, is_enabled_)
      << "Wrote SpdyRstStreamIR:" << FORMAT_ARG(connection_id_)
      << FORMAT_ATTR(rst_stream, stream_id)
      << " error_code=" << spdy::ErrorCodeToString(rst_stream.error_code());
}

void Http2FrameLogger::VisitSettings(const SpdySettingsIR& settings) {
  HTTP2_TRACE_LOG(perspective_, is_enabled_)
      << "Wrote SpdySettingsIR:" << FORMAT_ARG(connection_id_)
      << FORMAT_ATTR(settings, is_ack)
      << " values=" << LogContainer(settings.values(), LogSettingsEntry());
}

void Http2FrameLogger::VisitPing(const SpdyPingIR& ping) {
  HTTP2_TRACE_LOG(perspective_, is_enabled_)
      << "Wrote SpdyPingIR:" << FORMAT_ARG(connection_id_)
      << FORMAT_ATTR(ping, id) << FORMAT_ATTR(ping, is_ack);
}

void Http2FrameLogger::VisitGoAway(const SpdyGoAwayIR& goaway) {
  HTTP2_TRACE_LOG(perspective_, is_enabled_)
      << "Wrote SpdyGoAwayIR:" << FORMAT_ARG(connection_id_)
      << FORMAT_ATTR(goaway, last_good_stream_id)
      << " error_code=" << spdy::ErrorCodeToString(goaway.error_code())
      << FORMAT_ATTR(goaway, description);
}

void Http2FrameLogger::VisitHeaders(const SpdyHeadersIR& headers) {
  HTTP2_TRACE_LOG(perspective_, is_enabled_)
      << "Wrote SpdyHeadersIR:" << FORMAT_ARG(connection_id_)
      << FORMAT_ATTR(headers, stream_id) << FORMAT_ATTR(headers, fin)
      << FORMAT_ATTR(headers, has_priority) << FORMAT_INT_ATTR(headers, weight)
      << FORMAT_ATTR(headers, parent_stream_id)
      << FORMAT_ATTR(headers, exclusive) << FORMAT_ATTR(headers, padded)
      << FORMAT_ATTR(headers, padding_payload_len)
      << FORMAT_HEADER_BLOCK(headers);
}

void Http2FrameLogger::VisitWindowUpdate(
    const SpdyWindowUpdateIR& window_update) {
  HTTP2_TRACE_LOG(perspective_, is_enabled_)
      << "Wrote SpdyWindowUpdateIR:" << FORMAT_ARG(connection_id_)
      << FORMAT_ATTR(window_update, stream_id)
      << FORMAT_ATTR(window_update, delta);
}

void Http2FrameLogger::VisitPushPromise(const SpdyPushPromiseIR& push_promise) {
  HTTP2_TRACE_LOG(perspective_, is_enabled_)
      << "Wrote SpdyPushPromiseIR:" << FORMAT_ARG(connection_id_)
      << FORMAT_ATTR(push_promise, stream_id) << FORMAT_ATTR(push_promise, fin)
      << FORMAT_ATTR(push_promise, promised_stream_id)
      << FORMAT_ATTR(push_promise, padded)
      << FORMAT_ATTR(push_promise, padding_payload_len)
      << FORMAT_HEADER_BLOCK(push_promise);
}

void Http2FrameLogger::VisitContinuation(
    const SpdyContinuationIR& continuation) {
  HTTP2_TRACE_LOG(perspective_, is_enabled_)
      << "Wrote SpdyContinuationIR:" << FORMAT_ARG(connection_id_)
      << FORMAT_ATTR(continuation, stream_id)
      << FORMAT_ATTR(continuation, end_headers);
}

void Http2FrameLogger::VisitAltSvc(const SpdyAltSvcIR& altsvc) {
  HTTP2_TRACE_LOG(perspective_, is_enabled_)
      << "Wrote SpdyAltSvcIR:" << FORMAT_ARG(connection_id_)
      << FORMAT_ATTR(altsvc, stream_id) << FORMAT_ATTR(altsvc, origin)
      << " altsvc_vector="
      << LogContainer(altsvc.altsvc_vector(), LogAlternativeService());
}

void Http2FrameLogger::VisitPriority(const SpdyPriorityIR& priority) {
  HTTP2_TRACE_LOG(perspective_, is_enabled_)
      << "Wrote SpdyPriorityIR:" << FORMAT_ARG(connection_id_)
      << FORMAT_ATTR(priority, stream_id)
      << FORMAT_ATTR(priority, parent_stream_id)
      << FORMAT_INT_ATTR(priority, weight) << FORMAT_ATTR(priority, exclusive);
}

void Http2FrameLogger::VisitData(const SpdyDataIR& data) {
  HTTP2_TRACE_LOG(perspective_, is_enabled_)
      << "Wrote SpdyDataIR:" << FORMAT_ARG(connection_id_)
      << FORMAT_ATTR(data, stream_id) << FORMAT_ATTR(data, fin)
      << " data_len=" << data.data_len() << FORMAT_ATTR(data, padded)
      << FORMAT_ATTR(data, padding_payload_len);
}

void Http2FrameLogger::VisitPriorityUpdate(
    const spdy::SpdyPriorityUpdateIR& priority_update) {
  HTTP2_TRACE_LOG(perspective_, is_enabled_)
      << "Wrote SpdyPriorityUpdateIR:" << FORMAT_ARG(connection_id_)
      << FORMAT_ATTR(priority_update, stream_id)
      << FORMAT_ATTR(priority_update, prioritized_stream_id)
      << FORMAT_ATTR(priority_update, priority_field_value);
}

void Http2FrameLogger::VisitAcceptCh(
    const spdy::SpdyAcceptChIR& /*accept_ch*/) {
  QUICHE_BUG(bug_2794_2)
      << "Sending ACCEPT_CH frames is currently unimplemented.";
}

void Http2FrameLogger::VisitUnknown(const SpdyUnknownIR& ir) {
  HTTP2_TRACE_LOG(perspective_, is_enabled_)
      << "Wrote SpdyUnknownIR:" << FORMAT_ARG(connection_id_)
      << FORMAT_ATTR(ir, stream_id) << FORMAT_INT_ATTR(ir, type)
      << FORMAT_INT_ATTR(ir, flags) << FORMAT_ATTR(ir, length);
}

}  // namespace http2
