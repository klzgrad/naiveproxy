#include "http2/adapter/callback_visitor.h"

#include "http2/adapter/nghttp2_util.h"
#include "third_party/nghttp2/src/lib/includes/nghttp2/nghttp2.h"
#include "common/quiche_endian.h"

// This visitor implementation needs visibility into the
// nghttp2_session_callbacks type. There's no public header, so we'll redefine
// the struct here.
struct nghttp2_session_callbacks {
  nghttp2_send_callback send_callback;
  nghttp2_recv_callback recv_callback;
  nghttp2_on_frame_recv_callback on_frame_recv_callback;
  nghttp2_on_invalid_frame_recv_callback on_invalid_frame_recv_callback;
  nghttp2_on_data_chunk_recv_callback on_data_chunk_recv_callback;
  nghttp2_before_frame_send_callback before_frame_send_callback;
  nghttp2_on_frame_send_callback on_frame_send_callback;
  nghttp2_on_frame_not_send_callback on_frame_not_send_callback;
  nghttp2_on_stream_close_callback on_stream_close_callback;
  nghttp2_on_begin_headers_callback on_begin_headers_callback;
  nghttp2_on_header_callback on_header_callback;
  nghttp2_on_header_callback2 on_header_callback2;
  nghttp2_on_invalid_header_callback on_invalid_header_callback;
  nghttp2_on_invalid_header_callback2 on_invalid_header_callback2;
  nghttp2_select_padding_callback select_padding_callback;
  nghttp2_data_source_read_length_callback read_length_callback;
  nghttp2_on_begin_frame_callback on_begin_frame_callback;
  nghttp2_send_data_callback send_data_callback;
  nghttp2_pack_extension_callback pack_extension_callback;
  nghttp2_unpack_extension_callback unpack_extension_callback;
  nghttp2_on_extension_chunk_recv_callback on_extension_chunk_recv_callback;
  nghttp2_error_callback error_callback;
  nghttp2_error_callback2 error_callback2;
};

namespace http2 {
namespace adapter {

void CallbackVisitor::OnConnectionError() {
  QUICHE_LOG(FATAL) << "Not implemented";
}

void CallbackVisitor::OnFrameHeader(Http2StreamId stream_id,
                                    size_t length,
                                    uint8_t type,
                                    uint8_t flags) {
  // The general strategy is to clear |current_frame_| at the start of a new
  // frame, accumulate frame information from the various callback events, then
  // invoke the on_frame_recv_callback() with the accumulated frame data.
  memset(&current_frame_, 0, sizeof(current_frame_));
  current_frame_.hd.stream_id = stream_id;
  current_frame_.hd.length = length;
  current_frame_.hd.type = type;
  current_frame_.hd.flags = flags;
  callbacks_->on_begin_frame_callback(nullptr, &current_frame_.hd, user_data_);
}

void CallbackVisitor::OnSettingsStart() {}

void CallbackVisitor::OnSetting(Http2Setting setting) {
  settings_.push_back({.settings_id = setting.id, .value = setting.value});
}

void CallbackVisitor::OnSettingsEnd() {
  current_frame_.settings.niv = settings_.size();
  current_frame_.settings.iv = settings_.data();
  callbacks_->on_frame_recv_callback(nullptr, &current_frame_, user_data_);
  settings_.clear();
}

void CallbackVisitor::OnSettingsAck() {
  // ACK is part of the flags, which were set in OnFrameHeader().
  callbacks_->on_frame_recv_callback(nullptr, &current_frame_, user_data_);
}

void CallbackVisitor::OnBeginHeadersForStream(Http2StreamId stream_id) {
  auto it = stream_map_.find(stream_id);
  if (it == stream_map_.end()) {
    auto p = stream_map_.insert({stream_id, absl::make_unique<StreamInfo>()});
    it = p.first;
  }
  if (it->second->received_headers) {
    // At least one headers frame has already been received.
    current_frame_.headers.cat = NGHTTP2_HCAT_HEADERS;
  } else {
    switch (perspective_) {
      case Perspective::kClient:
        current_frame_.headers.cat = NGHTTP2_HCAT_RESPONSE;
        break;
      case Perspective::kServer:
        current_frame_.headers.cat = NGHTTP2_HCAT_REQUEST;
        break;
    }
  }
  callbacks_->on_begin_headers_callback(nullptr, &current_frame_, user_data_);
  it->second->received_headers = true;
}

void CallbackVisitor::OnHeaderForStream(Http2StreamId stream_id,
                                        absl::string_view name,
                                        absl::string_view value) {
  callbacks_->on_header_callback(
      nullptr, &current_frame_, ToUint8Ptr(name.data()), name.size(),
      ToUint8Ptr(value.data()), value.size(), NGHTTP2_NV_FLAG_NONE, user_data_);
}

void CallbackVisitor::OnEndHeadersForStream(Http2StreamId stream_id) {
  callbacks_->on_frame_recv_callback(nullptr, &current_frame_, user_data_);
}

void CallbackVisitor::OnBeginDataForStream(Http2StreamId stream_id,
                                           size_t payload_length) {
  // TODO(b/181586191): Interpret padding, subtract padding from
  // |remaining_data_|.
  remaining_data_ = payload_length;
  if (remaining_data_ == 0) {
    callbacks_->on_frame_recv_callback(nullptr, &current_frame_, user_data_);
  }
}

void CallbackVisitor::OnDataForStream(Http2StreamId stream_id,
                                      absl::string_view data) {
  callbacks_->on_data_chunk_recv_callback(nullptr, current_frame_.hd.flags,
                                          stream_id, ToUint8Ptr(data.data()),
                                          data.size(), user_data_);
  remaining_data_ -= data.size();
  if (remaining_data_ == 0) {
    callbacks_->on_frame_recv_callback(nullptr, &current_frame_, user_data_);
  }
}

void CallbackVisitor::OnEndStream(Http2StreamId stream_id) {}

void CallbackVisitor::OnRstStream(Http2StreamId stream_id,
                                  Http2ErrorCode error_code) {
  current_frame_.rst_stream.error_code = static_cast<uint32_t>(error_code);
  callbacks_->on_frame_recv_callback(nullptr, &current_frame_, user_data_);
}

void CallbackVisitor::OnCloseStream(Http2StreamId stream_id,
                                    Http2ErrorCode error_code) {
  callbacks_->on_stream_close_callback(
      nullptr, stream_id, static_cast<uint32_t>(error_code), user_data_);
}

void CallbackVisitor::OnPriorityForStream(Http2StreamId stream_id,
                                          Http2StreamId parent_stream_id,
                                          int weight,
                                          bool exclusive) {
  current_frame_.priority.pri_spec.stream_id = parent_stream_id;
  current_frame_.priority.pri_spec.weight = weight;
  current_frame_.priority.pri_spec.exclusive = exclusive;
  callbacks_->on_frame_recv_callback(nullptr, &current_frame_, user_data_);
}

void CallbackVisitor::OnPing(Http2PingId ping_id, bool is_ack) {
  uint64_t network_order_opaque_data =
      quiche::QuicheEndian::HostToNet64(ping_id);
  std::memcpy(current_frame_.ping.opaque_data, &network_order_opaque_data,
              sizeof(network_order_opaque_data));
  callbacks_->on_frame_recv_callback(nullptr, &current_frame_, user_data_);
}

void CallbackVisitor::OnPushPromiseForStream(Http2StreamId stream_id,
                                             Http2StreamId promised_stream_id) {
  QUICHE_LOG(FATAL) << "Not implemented";
}

void CallbackVisitor::OnGoAway(Http2StreamId last_accepted_stream_id,
                               Http2ErrorCode error_code,
                               absl::string_view opaque_data) {
  current_frame_.goaway.last_stream_id = last_accepted_stream_id;
  current_frame_.goaway.error_code = static_cast<uint32_t>(error_code);
  current_frame_.goaway.opaque_data = ToUint8Ptr(opaque_data.data());
  current_frame_.goaway.opaque_data_len = opaque_data.size();
  callbacks_->on_frame_recv_callback(nullptr, &current_frame_, user_data_);
}

void CallbackVisitor::OnWindowUpdate(Http2StreamId stream_id,
                                     int window_increment) {
  current_frame_.window_update.window_size_increment = window_increment;
  callbacks_->on_frame_recv_callback(nullptr, &current_frame_, user_data_);
}

void CallbackVisitor::OnReadyToSendDataForStream(Http2StreamId stream_id,
                                                 char* destination_buffer,
                                                 size_t length,
                                                 ssize_t* written,
                                                 bool* end_stream) {
  QUICHE_LOG(FATAL) << "Not implemented";
}

void CallbackVisitor::OnReadyToSendMetadataForStream(Http2StreamId stream_id,
                                                     char* buffer,
                                                     size_t length,
                                                     ssize_t* written) {
  QUICHE_LOG(FATAL) << "Not implemented";
}

void CallbackVisitor::OnBeginMetadataForStream(Http2StreamId stream_id,
                                               size_t payload_length) {
  QUICHE_LOG(FATAL) << "Not implemented";
}

void CallbackVisitor::OnMetadataForStream(Http2StreamId stream_id,
                                          absl::string_view metadata) {
  QUICHE_LOG(FATAL) << "Not implemented";
}

void CallbackVisitor::OnMetadataEndForStream(Http2StreamId stream_id) {
  QUICHE_LOG(FATAL) << "Not implemented";
}

}  // namespace adapter
}  // namespace http2
