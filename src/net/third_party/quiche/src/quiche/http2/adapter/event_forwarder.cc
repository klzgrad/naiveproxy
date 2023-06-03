#include "quiche/http2/adapter/event_forwarder.h"

namespace http2 {
namespace adapter {

EventForwarder::EventForwarder(ForwardPredicate can_forward,
                               spdy::SpdyFramerVisitorInterface& receiver)
    : can_forward_(std::move(can_forward)), receiver_(receiver) {}

void EventForwarder::OnError(Http2DecoderAdapter::SpdyFramerError error,
                             std::string detailed_error) {
  if (can_forward_()) {
    receiver_.OnError(error, std::move(detailed_error));
  }
}

void EventForwarder::OnCommonHeader(spdy::SpdyStreamId stream_id, size_t length,
                                    uint8_t type, uint8_t flags) {
  if (can_forward_()) {
    receiver_.OnCommonHeader(stream_id, length, type, flags);
  }
}

void EventForwarder::OnDataFrameHeader(spdy::SpdyStreamId stream_id,
                                       size_t length, bool fin) {
  if (can_forward_()) {
    receiver_.OnDataFrameHeader(stream_id, length, fin);
  }
}

void EventForwarder::OnStreamFrameData(spdy::SpdyStreamId stream_id,
                                       const char* data, size_t len) {
  if (can_forward_()) {
    receiver_.OnStreamFrameData(stream_id, data, len);
  }
}

void EventForwarder::OnStreamEnd(spdy::SpdyStreamId stream_id) {
  if (can_forward_()) {
    receiver_.OnStreamEnd(stream_id);
  }
}

void EventForwarder::OnStreamPadLength(spdy::SpdyStreamId stream_id,
                                       size_t value) {
  if (can_forward_()) {
    receiver_.OnStreamPadLength(stream_id, value);
  }
}

void EventForwarder::OnStreamPadding(spdy::SpdyStreamId stream_id, size_t len) {
  if (can_forward_()) {
    receiver_.OnStreamPadding(stream_id, len);
  }
}

spdy::SpdyHeadersHandlerInterface* EventForwarder::OnHeaderFrameStart(
    spdy::SpdyStreamId stream_id) {
  return receiver_.OnHeaderFrameStart(stream_id);
}

void EventForwarder::OnHeaderFrameEnd(spdy::SpdyStreamId stream_id) {
  if (can_forward_()) {
    receiver_.OnHeaderFrameEnd(stream_id);
  }
}

void EventForwarder::OnRstStream(spdy::SpdyStreamId stream_id,
                                 spdy::SpdyErrorCode error_code) {
  if (can_forward_()) {
    receiver_.OnRstStream(stream_id, error_code);
  }
}

void EventForwarder::OnSettings() {
  if (can_forward_()) {
    receiver_.OnSettings();
  }
}

void EventForwarder::OnSetting(spdy::SpdySettingsId id, uint32_t value) {
  if (can_forward_()) {
    receiver_.OnSetting(id, value);
  }
}

void EventForwarder::OnSettingsEnd() {
  if (can_forward_()) {
    receiver_.OnSettingsEnd();
  }
}

void EventForwarder::OnSettingsAck() {
  if (can_forward_()) {
    receiver_.OnSettingsAck();
  }
}

void EventForwarder::OnPing(spdy::SpdyPingId unique_id, bool is_ack) {
  if (can_forward_()) {
    receiver_.OnPing(unique_id, is_ack);
  }
}

void EventForwarder::OnGoAway(spdy::SpdyStreamId last_accepted_stream_id,
                              spdy::SpdyErrorCode error_code) {
  if (can_forward_()) {
    receiver_.OnGoAway(last_accepted_stream_id, error_code);
  }
}

bool EventForwarder::OnGoAwayFrameData(const char* goaway_data, size_t len) {
  if (can_forward_()) {
    return receiver_.OnGoAwayFrameData(goaway_data, len);
  }
  return false;
}

void EventForwarder::OnHeaders(spdy::SpdyStreamId stream_id,
                               size_t payload_length, bool has_priority,
                               int weight, spdy::SpdyStreamId parent_stream_id,
                               bool exclusive, bool fin, bool end) {
  if (can_forward_()) {
    receiver_.OnHeaders(stream_id, payload_length, has_priority, weight,
                        parent_stream_id, exclusive, fin, end);
  }
}

void EventForwarder::OnWindowUpdate(spdy::SpdyStreamId stream_id,
                                    int delta_window_size) {
  if (can_forward_()) {
    receiver_.OnWindowUpdate(stream_id, delta_window_size);
  }
}

void EventForwarder::OnPushPromise(spdy::SpdyStreamId stream_id,
                                   spdy::SpdyStreamId promised_stream_id,
                                   bool end) {
  if (can_forward_()) {
    receiver_.OnPushPromise(stream_id, promised_stream_id, end);
  }
}

void EventForwarder::OnContinuation(spdy::SpdyStreamId stream_id,
                                    size_t payload_length, bool end) {
  if (can_forward_()) {
    receiver_.OnContinuation(stream_id, payload_length, end);
  }
}

void EventForwarder::OnAltSvc(
    spdy::SpdyStreamId stream_id, absl::string_view origin,
    const spdy::SpdyAltSvcWireFormat::AlternativeServiceVector& altsvc_vector) {
  if (can_forward_()) {
    receiver_.OnAltSvc(stream_id, origin, altsvc_vector);
  }
}

void EventForwarder::OnPriority(spdy::SpdyStreamId stream_id,
                                spdy::SpdyStreamId parent_stream_id, int weight,
                                bool exclusive) {
  if (can_forward_()) {
    receiver_.OnPriority(stream_id, parent_stream_id, weight, exclusive);
  }
}

void EventForwarder::OnPriorityUpdate(spdy::SpdyStreamId prioritized_stream_id,
                                      absl::string_view priority_field_value) {
  if (can_forward_()) {
    receiver_.OnPriorityUpdate(prioritized_stream_id, priority_field_value);
  }
}

bool EventForwarder::OnUnknownFrame(spdy::SpdyStreamId stream_id,
                                    uint8_t frame_type) {
  if (can_forward_()) {
    return receiver_.OnUnknownFrame(stream_id, frame_type);
  }
  return false;
}

void EventForwarder::OnUnknownFrameStart(spdy::SpdyStreamId stream_id,
                                         size_t length, uint8_t type,
                                         uint8_t flags) {
  if (can_forward_()) {
    receiver_.OnUnknownFrameStart(stream_id, length, type, flags);
  }
}

void EventForwarder::OnUnknownFramePayload(spdy::SpdyStreamId stream_id,
                                           absl::string_view payload) {
  if (can_forward_()) {
    receiver_.OnUnknownFramePayload(stream_id, payload);
  }
}

}  // namespace adapter
}  // namespace http2
