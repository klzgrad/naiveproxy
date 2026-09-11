// Classes and utilities for supporting HTTP/2 trace logging, which logs
// information about all control and data frames sent and received over
// HTTP/2 connections.

#ifndef QUICHE_HTTP2_CORE_HTTP2_TRACE_LOGGING_H_
#define QUICHE_HTTP2_CORE_HTTP2_TRACE_LOGGING_H_

#include <cstdint>

#include "absl/strings/string_view.h"
#include "quiche/http2/core/http2_frame_decoder_adapter.h"
#include "quiche/http2/core/recording_headers_handler.h"
#include "quiche/http2/core/spdy_headers_handler_interface.h"
#include "quiche/http2/core/spdy_protocol.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_callbacks.h"

// Logging macro to use for all HTTP/2 trace logging. Iff trace logging is
// enabled, logs at level INFO with a common prefix prepended (to facilitate
// post-hoc filtering of trace logging output).
#define HTTP2_TRACE_LOG(perspective, is_enabled) \
  QUICHE_LOG_IF(INFO, is_enabled()) << "[HTTP2_TRACE " << perspective << "] "

namespace http2 {

// Intercepts deframing events to provide detailed logs. Intended to be used for
// manual debugging.
//
// Note any new methods in SpdyFramerVisitorInterface MUST be overridden here to
// properly forward the event. This could be ensured by making every event in
// SpdyFramerVisitorInterface a pure virtual.
class QUICHE_EXPORT Http2TraceLogger : public spdy::SpdyFramerVisitorInterface {
 public:
  typedef spdy::SpdyAltSvcWireFormat SpdyAltSvcWireFormat;
  typedef spdy::SpdyErrorCode SpdyErrorCode;
  typedef spdy::SpdyFramerVisitorInterface SpdyFramerVisitorInterface;
  typedef spdy::SpdyPingId SpdyPingId;
  typedef spdy::SpdyPriority SpdyPriority;
  typedef spdy::SpdySettingsId SpdySettingsId;
  typedef spdy::SpdyStreamId SpdyStreamId;

  Http2TraceLogger(SpdyFramerVisitorInterface* parent,
                   absl::string_view perspective,
                   quiche::MultiUseCallback<bool()> is_enabled,
                   const void* connection_id);
  ~Http2TraceLogger() override;

  Http2TraceLogger(const Http2TraceLogger&) = delete;
  Http2TraceLogger& operator=(const Http2TraceLogger&) = delete;

  void OnError(http2::Http2DecoderAdapter::SpdyFramerError error,
               std::string detailed_error) override;
  void OnCommonHeader(SpdyStreamId stream_id, size_t length, uint8_t type,
                      uint8_t flags) override;
  spdy::SpdyHeadersHandlerInterface* OnHeaderFrameStart(
      SpdyStreamId stream_id) override;
  void OnHeaderFrameEnd(SpdyStreamId stream_id) override;
  void OnDataFrameHeader(SpdyStreamId stream_id, size_t length,
                         bool fin) override;
  void OnStreamFrameData(SpdyStreamId stream_id, const char* data,
                         size_t len) override;
  void OnStreamEnd(SpdyStreamId stream_id) override;
  void OnStreamPadLength(SpdyStreamId stream_id, size_t value) override;
  void OnStreamPadding(SpdyStreamId stream_id, size_t len) override;
  void OnRstStream(SpdyStreamId stream_id, SpdyErrorCode error_code) override;
  void OnSetting(spdy::SpdySettingsId id, uint32_t value) override;
  void OnPing(SpdyPingId unique_id, bool is_ack) override;
  void OnSettings() override;
  void OnSettingsEnd() override;
  void OnSettingsAck() override;
  void OnGoAway(SpdyStreamId last_accepted_stream_id,
                SpdyErrorCode error_code) override;
  bool OnGoAwayFrameData(const char* goaway_data, size_t len) override;
  void OnHeaders(SpdyStreamId stream_id, size_t payload_length,
                 bool has_priority, int weight, SpdyStreamId parent_stream_id,
                 bool exclusive, bool fin, bool end) override;
  void OnWindowUpdate(SpdyStreamId stream_id, int delta_window_size) override;
  void OnPushPromise(SpdyStreamId stream_id, SpdyStreamId promised_stream_id,
                     bool end) override;
  void OnContinuation(SpdyStreamId stream_id, size_t payload_length,
                      bool end) override;
  void OnAltSvc(SpdyStreamId stream_id, absl::string_view origin,
                const SpdyAltSvcWireFormat::AlternativeServiceVector&
                    altsvc_vector) override;
  void OnPriority(SpdyStreamId stream_id, SpdyStreamId parent_stream_id,
                  int weight, bool exclusive) override;
  void OnPriorityUpdate(SpdyStreamId prioritized_stream_id,
                        absl::string_view priority_field_value) override;
  bool OnUnknownFrame(SpdyStreamId stream_id, uint8_t frame_type) override;
  void OnUnknownFrameStart(SpdyStreamId stream_id, size_t length, uint8_t type,
                           uint8_t flags) override;
  void OnUnknownFramePayload(SpdyStreamId stream_id,
                             absl::string_view payload) override;

 private:
  void LogReceivedHeaders() const;

  std::unique_ptr<spdy::RecordingHeadersHandler> recording_headers_handler_;

  SpdyFramerVisitorInterface* wrapped_;
  const absl::string_view perspective_;
  const quiche::MultiUseCallback<bool()> is_enabled_;
  const void* connection_id_;
};

// Visitor to log control frames that have been written.
class QUICHE_EXPORT Http2FrameLogger : public spdy::SpdyFrameVisitor {
 public:
  // This class will preface all of its log messages with the value of
  // |connection_id| in hexadecimal.
  Http2FrameLogger(absl::string_view perspective,
                   quiche::MultiUseCallback<bool()> is_enabled,
                   const void* connection_id)
      : perspective_(perspective),
        is_enabled_(std::move(is_enabled)),
        connection_id_(connection_id) {}

  Http2FrameLogger(const Http2FrameLogger&) = delete;
  Http2FrameLogger& operator=(const Http2FrameLogger&) = delete;

  void VisitRstStream(const spdy::SpdyRstStreamIR& rst_stream) override;
  void VisitSettings(const spdy::SpdySettingsIR& settings) override;
  void VisitPing(const spdy::SpdyPingIR& ping) override;
  void VisitGoAway(const spdy::SpdyGoAwayIR& goaway) override;
  void VisitHeaders(const spdy::SpdyHeadersIR& headers) override;
  void VisitWindowUpdate(
      const spdy::SpdyWindowUpdateIR& window_update) override;
  void VisitPushPromise(const spdy::SpdyPushPromiseIR& push_promise) override;
  void VisitContinuation(const spdy::SpdyContinuationIR& continuation) override;
  void VisitAltSvc(const spdy::SpdyAltSvcIR& altsvc) override;
  void VisitPriority(const spdy::SpdyPriorityIR& priority) override;
  void VisitData(const spdy::SpdyDataIR& data) override;
  void VisitPriorityUpdate(
      const spdy::SpdyPriorityUpdateIR& priority_update) override;
  void VisitAcceptCh(const spdy::SpdyAcceptChIR& accept_ch) override;
  void VisitUnknown(const spdy::SpdyUnknownIR& ir) override;

 private:
  const absl::string_view perspective_;
  const quiche::MultiUseCallback<bool()> is_enabled_;
  const void* connection_id_;
};

}  // namespace http2

#endif  // QUICHE_HTTP2_CORE_HTTP2_TRACE_LOGGING_H_
