#ifndef QUICHE_HTTP2_ADAPTER_EVENT_FORWARDER_H_
#define QUICHE_HTTP2_ADAPTER_EVENT_FORWARDER_H_

#include <functional>

#include "quiche/http2/core/http2_frame_decoder_adapter.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_callbacks.h"

namespace http2 {
namespace adapter {

// Forwards events to a provided SpdyFramerVisitorInterface receiver if the
// provided predicate succeeds. Currently, OnHeaderFrameStart() is always
// forwarded regardless of the predicate.
// TODO(diannahu): Add a NoOpHeadersHandler if needed.
class QUICHE_EXPORT EventForwarder : public spdy::SpdyFramerVisitorInterface {
 public:
  // Whether the forwarder can forward events to the receiver.
  using ForwardPredicate = quiche::MultiUseCallback<bool()>;

  EventForwarder(ForwardPredicate can_forward,
                 spdy::SpdyFramerVisitorInterface& receiver);

  void OnError(Http2DecoderAdapter::SpdyFramerError error,
               std::string detailed_error) override;
  void OnCommonHeader(spdy::SpdyStreamId stream_id, size_t length, uint8_t type,
                      uint8_t flags) override;
  void OnDataFrameHeader(spdy::SpdyStreamId stream_id, size_t length,
                         bool fin) override;
  void OnStreamFrameData(spdy::SpdyStreamId stream_id, const char* data,
                         size_t len) override;
  void OnStreamEnd(spdy::SpdyStreamId stream_id) override;
  void OnStreamPadLength(spdy::SpdyStreamId stream_id, size_t value) override;
  void OnStreamPadding(spdy::SpdyStreamId stream_id, size_t len) override;
  spdy::SpdyHeadersHandlerInterface* OnHeaderFrameStart(
      spdy::SpdyStreamId stream_id) override;
  void OnHeaderFrameEnd(spdy::SpdyStreamId stream_id) override;
  void OnRstStream(spdy::SpdyStreamId stream_id,
                   spdy::SpdyErrorCode error_code) override;
  void OnSettings() override;
  void OnSetting(spdy::SpdySettingsId id, uint32_t value) override;
  void OnSettingsEnd() override;
  void OnSettingsAck() override;
  void OnPing(spdy::SpdyPingId unique_id, bool is_ack) override;
  void OnGoAway(spdy::SpdyStreamId last_accepted_stream_id,
                spdy::SpdyErrorCode error_code) override;
  bool OnGoAwayFrameData(const char* goaway_data, size_t len) override;
  void OnHeaders(spdy::SpdyStreamId stream_id, size_t payload_length,
                 bool has_priority, int weight,
                 spdy::SpdyStreamId parent_stream_id, bool exclusive, bool fin,
                 bool end) override;
  void OnWindowUpdate(spdy::SpdyStreamId stream_id,
                      int delta_window_size) override;
  void OnPushPromise(spdy::SpdyStreamId stream_id,
                     spdy::SpdyStreamId promised_stream_id, bool end) override;
  void OnContinuation(spdy::SpdyStreamId stream_id, size_t payload_length,
                      bool end) override;
  void OnAltSvc(spdy::SpdyStreamId stream_id, absl::string_view origin,
                const spdy::SpdyAltSvcWireFormat::AlternativeServiceVector&
                    altsvc_vector) override;
  void OnPriority(spdy::SpdyStreamId stream_id,
                  spdy::SpdyStreamId parent_stream_id, int weight,
                  bool exclusive) override;
  void OnPriorityUpdate(spdy::SpdyStreamId prioritized_stream_id,
                        absl::string_view priority_field_value) override;
  bool OnUnknownFrame(spdy::SpdyStreamId stream_id,
                      uint8_t frame_type) override;
  void OnUnknownFrameStart(spdy::SpdyStreamId stream_id, size_t length,
                           uint8_t type, uint8_t flags) override;
  void OnUnknownFramePayload(spdy::SpdyStreamId stream_id,
                             absl::string_view payload) override;

 private:
  ForwardPredicate can_forward_;
  spdy::SpdyFramerVisitorInterface& receiver_;
};

}  // namespace adapter
}  // namespace http2

#endif  // QUICHE_HTTP2_ADAPTER_EVENT_FORWARDER_H_
