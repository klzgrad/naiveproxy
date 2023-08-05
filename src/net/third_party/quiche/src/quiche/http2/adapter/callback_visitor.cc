#include "quiche/http2/adapter/callback_visitor.h"

#include "absl/strings/escaping.h"
#include "quiche/http2/adapter/http2_util.h"
#include "quiche/http2/adapter/nghttp2_util.h"
#include "quiche/common/quiche_endian.h"

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

CallbackVisitor::CallbackVisitor(Perspective perspective,
                                 const nghttp2_session_callbacks& callbacks,
                                 void* user_data)
    : perspective_(perspective),
      callbacks_(MakeCallbacksPtr(nullptr)),
      user_data_(user_data) {
  nghttp2_session_callbacks* c;
  nghttp2_session_callbacks_new(&c);
  *c = callbacks;
  callbacks_ = MakeCallbacksPtr(c);
  memset(&current_frame_, 0, sizeof(current_frame_));
}

int64_t CallbackVisitor::OnReadyToSend(absl::string_view serialized) {
  if (!callbacks_->send_callback) {
    return kSendError;
  }
  int64_t result = callbacks_->send_callback(
      nullptr, ToUint8Ptr(serialized.data()), serialized.size(), 0, user_data_);
  QUICHE_VLOG(1) << "CallbackVisitor::OnReadyToSend called with "
                 << serialized.size() << " bytes, returning " << result;
  QUICHE_VLOG(2) << (perspective_ == Perspective::kClient ? "Client" : "Server")
                 << " sending: [" << absl::CEscape(serialized) << "]";
  if (result > 0) {
    return result;
  } else if (result == NGHTTP2_ERR_WOULDBLOCK) {
    return kSendBlocked;
  } else {
    return kSendError;
  }
}

void CallbackVisitor::OnConnectionError(ConnectionError /*error*/) {
  QUICHE_VLOG(1) << "OnConnectionError not implemented";
}

bool CallbackVisitor::OnFrameHeader(Http2StreamId stream_id, size_t length,
                                    uint8_t type, uint8_t flags) {
  QUICHE_VLOG(1) << "CallbackVisitor::OnFrameHeader(stream_id=" << stream_id
                 << ", type=" << int(type) << ", length=" << length
                 << ", flags=" << int(flags) << ")";
  if (static_cast<FrameType>(type) == FrameType::CONTINUATION) {
    if (static_cast<FrameType>(current_frame_.hd.type) != FrameType::HEADERS ||
        current_frame_.hd.stream_id == 0 ||
        current_frame_.hd.stream_id != stream_id) {
      // CONTINUATION frames must follow HEADERS on the same stream. If no
      // frames have been received, the type is initialized to zero, and the
      // comparison will fail.
      return false;
    }
    current_frame_.hd.length += length;
    current_frame_.hd.flags |= flags;
    QUICHE_DLOG_IF(ERROR, length == 0) << "Empty CONTINUATION!";
    // Still need to deliver the CONTINUATION to the begin frame callback.
    nghttp2_frame_hd hd;
    memset(&hd, 0, sizeof(hd));
    hd.stream_id = stream_id;
    hd.length = length;
    hd.type = type;
    hd.flags = flags;
    if (callbacks_->on_begin_frame_callback) {
      const int result =
          callbacks_->on_begin_frame_callback(nullptr, &hd, user_data_);
      return result == 0;
    }
    return true;
  }
  // The general strategy is to clear |current_frame_| at the start of a new
  // frame, accumulate frame information from the various callback events, then
  // invoke the on_frame_recv_callback() with the accumulated frame data.
  memset(&current_frame_, 0, sizeof(current_frame_));
  current_frame_.hd.stream_id = stream_id;
  current_frame_.hd.length = length;
  current_frame_.hd.type = type;
  current_frame_.hd.flags = flags;
  if (callbacks_->on_begin_frame_callback) {
    const int result = callbacks_->on_begin_frame_callback(
        nullptr, &current_frame_.hd, user_data_);
    return result == 0;
  }
  return true;
}

void CallbackVisitor::OnSettingsStart() {}

void CallbackVisitor::OnSetting(Http2Setting setting) {
  settings_.push_back({setting.id, setting.value});
}

void CallbackVisitor::OnSettingsEnd() {
  current_frame_.settings.niv = settings_.size();
  current_frame_.settings.iv = settings_.data();
  QUICHE_VLOG(1) << "OnSettingsEnd, received settings of size "
                 << current_frame_.settings.niv;
  if (callbacks_->on_frame_recv_callback) {
    const int result = callbacks_->on_frame_recv_callback(
        nullptr, &current_frame_, user_data_);
    QUICHE_DCHECK_EQ(0, result);
  }
  settings_.clear();
}

void CallbackVisitor::OnSettingsAck() {
  // ACK is part of the flags, which were set in OnFrameHeader().
  QUICHE_VLOG(1) << "OnSettingsAck()";
  if (callbacks_->on_frame_recv_callback) {
    const int result = callbacks_->on_frame_recv_callback(
        nullptr, &current_frame_, user_data_);
    QUICHE_DCHECK_EQ(0, result);
  }
}

bool CallbackVisitor::OnBeginHeadersForStream(Http2StreamId stream_id) {
  auto it = GetStreamInfo(stream_id);
  if (it->second.received_headers) {
    // At least one headers frame has already been received.
    QUICHE_VLOG(1)
        << "Headers already received for stream " << stream_id
        << ", these are trailers or headers following a 100 response";
    current_frame_.headers.cat = NGHTTP2_HCAT_HEADERS;
  } else {
    switch (perspective_) {
      case Perspective::kClient:
        QUICHE_VLOG(1) << "First headers at the client for stream " << stream_id
                       << "; these are response headers";
        current_frame_.headers.cat = NGHTTP2_HCAT_RESPONSE;
        break;
      case Perspective::kServer:
        QUICHE_VLOG(1) << "First headers at the server for stream " << stream_id
                       << "; these are request headers";
        current_frame_.headers.cat = NGHTTP2_HCAT_REQUEST;
        break;
    }
  }
  it->second.received_headers = true;
  if (callbacks_->on_begin_headers_callback) {
    const int result = callbacks_->on_begin_headers_callback(
        nullptr, &current_frame_, user_data_);
    return result == 0;
  }
  return true;
}

Http2VisitorInterface::OnHeaderResult CallbackVisitor::OnHeaderForStream(
    Http2StreamId stream_id, absl::string_view name, absl::string_view value) {
  QUICHE_VLOG(2) << "OnHeaderForStream(stream_id=" << stream_id << ", name=["
                 << absl::CEscape(name) << "], value=[" << absl::CEscape(value)
                 << "])";
  if (callbacks_->on_header_callback) {
    const int result = callbacks_->on_header_callback(
        nullptr, &current_frame_, ToUint8Ptr(name.data()), name.size(),
        ToUint8Ptr(value.data()), value.size(), NGHTTP2_NV_FLAG_NONE,
        user_data_);
    if (result == 0) {
      return HEADER_OK;
    } else if (result == NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE) {
      return HEADER_RST_STREAM;
    } else {
      // Assume NGHTTP2_ERR_CALLBACK_FAILURE.
      return HEADER_CONNECTION_ERROR;
    }
  }
  return HEADER_OK;
}

bool CallbackVisitor::OnEndHeadersForStream(Http2StreamId stream_id) {
  QUICHE_VLOG(1) << "OnEndHeadersForStream(stream_id=" << stream_id << ")";
  if (callbacks_->on_frame_recv_callback) {
    const int result = callbacks_->on_frame_recv_callback(
        nullptr, &current_frame_, user_data_);
    return result == 0;
  }
  return true;
}

bool CallbackVisitor::OnDataPaddingLength(Http2StreamId /*stream_id*/,
                                          size_t padding_length) {
  QUICHE_DCHECK_GE(remaining_data_, padding_length);
  current_frame_.data.padlen = padding_length;
  remaining_data_ -= padding_length;
  if (remaining_data_ == 0 &&
      (current_frame_.hd.flags & NGHTTP2_FLAG_END_STREAM) == 0 &&
      callbacks_->on_frame_recv_callback != nullptr) {
    const int result = callbacks_->on_frame_recv_callback(
        nullptr, &current_frame_, user_data_);
    return result == 0;
  }
  return true;
}

bool CallbackVisitor::OnBeginDataForStream(Http2StreamId /*stream_id*/,
                                           size_t payload_length) {
  remaining_data_ = payload_length;
  if (remaining_data_ == 0 &&
      (current_frame_.hd.flags & NGHTTP2_FLAG_END_STREAM) == 0 &&
      callbacks_->on_frame_recv_callback != nullptr) {
    const int result = callbacks_->on_frame_recv_callback(
        nullptr, &current_frame_, user_data_);
    return result == 0;
  }
  return true;
}

bool CallbackVisitor::OnDataForStream(Http2StreamId stream_id,
                                      absl::string_view data) {
  QUICHE_VLOG(1) << "OnDataForStream(stream_id=" << stream_id
                 << ", data.size()=" << data.size() << ")";
  int result = 0;
  if (callbacks_->on_data_chunk_recv_callback) {
    result = callbacks_->on_data_chunk_recv_callback(
        nullptr, current_frame_.hd.flags, stream_id, ToUint8Ptr(data.data()),
        data.size(), user_data_);
  }
  remaining_data_ -= data.size();
  if (result == 0 && remaining_data_ == 0 &&
      (current_frame_.hd.flags & NGHTTP2_FLAG_END_STREAM) == 0 &&
      callbacks_->on_frame_recv_callback) {
    // If the DATA frame contains the END_STREAM flag, `on_frame_recv` is
    // invoked later.
    result = callbacks_->on_frame_recv_callback(nullptr, &current_frame_,
                                                user_data_);
  }
  return result == 0;
}

bool CallbackVisitor::OnEndStream(Http2StreamId stream_id) {
  QUICHE_VLOG(1) << "OnEndStream(stream_id=" << stream_id << ")";
  int result = 0;
  if (static_cast<FrameType>(current_frame_.hd.type) == FrameType::DATA &&
      (current_frame_.hd.flags & NGHTTP2_FLAG_END_STREAM) != 0 &&
      callbacks_->on_frame_recv_callback) {
    // `on_frame_recv` is invoked here to ensure that the Http2Adapter
    // implementation has successfully validated and processed the entire DATA
    // frame.
    result = callbacks_->on_frame_recv_callback(nullptr, &current_frame_,
                                                user_data_);
  }
  return result == 0;
}

void CallbackVisitor::OnRstStream(Http2StreamId stream_id,
                                  Http2ErrorCode error_code) {
  QUICHE_VLOG(1) << "OnRstStream(stream_id=" << stream_id
                 << ", error_code=" << static_cast<int>(error_code) << ")";
  current_frame_.rst_stream.error_code = static_cast<uint32_t>(error_code);
  if (callbacks_->on_frame_recv_callback) {
    const int result = callbacks_->on_frame_recv_callback(
        nullptr, &current_frame_, user_data_);
    QUICHE_DCHECK_EQ(0, result);
  }
}

bool CallbackVisitor::OnCloseStream(Http2StreamId stream_id,
                                    Http2ErrorCode error_code) {
  QUICHE_VLOG(1) << "OnCloseStream(stream_id=" << stream_id
                 << ", error_code=" << static_cast<int>(error_code) << ")";
  int result = 0;
  if (callbacks_->on_stream_close_callback) {
    result = callbacks_->on_stream_close_callback(
        nullptr, stream_id, static_cast<uint32_t>(error_code), user_data_);
  }
  stream_map_.erase(stream_id);
  if (stream_close_listener_) {
    stream_close_listener_(stream_id);
  }
  return result == 0;
}

void CallbackVisitor::OnPriorityForStream(Http2StreamId /*stream_id*/,
                                          Http2StreamId parent_stream_id,
                                          int weight, bool exclusive) {
  current_frame_.priority.pri_spec.stream_id = parent_stream_id;
  current_frame_.priority.pri_spec.weight = weight;
  current_frame_.priority.pri_spec.exclusive = exclusive;
  if (callbacks_->on_frame_recv_callback) {
    const int result = callbacks_->on_frame_recv_callback(
        nullptr, &current_frame_, user_data_);
    QUICHE_DCHECK_EQ(0, result);
  }
}

void CallbackVisitor::OnPing(Http2PingId ping_id, bool is_ack) {
  QUICHE_VLOG(1) << "OnPing(ping_id=" << static_cast<int64_t>(ping_id)
                 << ", is_ack=" << is_ack << ")";
  uint64_t network_order_opaque_data =
      quiche::QuicheEndian::HostToNet64(ping_id);
  std::memcpy(current_frame_.ping.opaque_data, &network_order_opaque_data,
              sizeof(network_order_opaque_data));
  if (callbacks_->on_frame_recv_callback) {
    const int result = callbacks_->on_frame_recv_callback(
        nullptr, &current_frame_, user_data_);
    QUICHE_DCHECK_EQ(0, result);
  }
}

void CallbackVisitor::OnPushPromiseForStream(
    Http2StreamId /*stream_id*/, Http2StreamId /*promised_stream_id*/) {
  QUICHE_LOG(DFATAL) << "Not implemented";
}

bool CallbackVisitor::OnGoAway(Http2StreamId last_accepted_stream_id,
                               Http2ErrorCode error_code,
                               absl::string_view opaque_data) {
  QUICHE_VLOG(1) << "OnGoAway(last_accepted_stream_id="
                 << last_accepted_stream_id
                 << ", error_code=" << static_cast<int>(error_code)
                 << ", opaque_data=[" << absl::CEscape(opaque_data) << "])";
  current_frame_.goaway.last_stream_id = last_accepted_stream_id;
  current_frame_.goaway.error_code = static_cast<uint32_t>(error_code);
  current_frame_.goaway.opaque_data = ToUint8Ptr(opaque_data.data());
  current_frame_.goaway.opaque_data_len = opaque_data.size();
  if (callbacks_->on_frame_recv_callback) {
    const int result = callbacks_->on_frame_recv_callback(
        nullptr, &current_frame_, user_data_);
    return result == 0;
  }
  return true;
}

void CallbackVisitor::OnWindowUpdate(Http2StreamId stream_id,
                                     int window_increment) {
  QUICHE_VLOG(1) << "OnWindowUpdate(stream_id=" << stream_id
                 << ", delta=" << window_increment << ")";
  current_frame_.window_update.window_size_increment = window_increment;
  if (callbacks_->on_frame_recv_callback) {
    const int result = callbacks_->on_frame_recv_callback(
        nullptr, &current_frame_, user_data_);
    QUICHE_DCHECK_EQ(0, result);
  }
}

void CallbackVisitor::PopulateFrame(nghttp2_frame& frame, uint8_t frame_type,
                                    Http2StreamId stream_id, size_t length,
                                    uint8_t flags, uint32_t error_code,
                                    bool sent_headers) {
  frame.hd.type = frame_type;
  frame.hd.stream_id = stream_id;
  frame.hd.length = length;
  frame.hd.flags = flags;
  const FrameType frame_type_enum = static_cast<FrameType>(frame_type);
  if (frame_type_enum == FrameType::HEADERS) {
    if (sent_headers) {
      frame.headers.cat = NGHTTP2_HCAT_HEADERS;
    } else {
      switch (perspective_) {
        case Perspective::kClient:
          QUICHE_VLOG(1) << "First headers sent by the client for stream "
                         << stream_id << "; these are request headers";
          frame.headers.cat = NGHTTP2_HCAT_REQUEST;
          break;
        case Perspective::kServer:
          QUICHE_VLOG(1) << "First headers sent by the server for stream "
                         << stream_id << "; these are response headers";
          frame.headers.cat = NGHTTP2_HCAT_RESPONSE;
          break;
      }
    }
  } else if (frame_type_enum == FrameType::RST_STREAM) {
    frame.rst_stream.error_code = error_code;
  } else if (frame_type_enum == FrameType::GOAWAY) {
    frame.goaway.error_code = error_code;
  }
}

int CallbackVisitor::OnBeforeFrameSent(uint8_t frame_type,
                                       Http2StreamId stream_id, size_t length,
                                       uint8_t flags) {
  QUICHE_VLOG(1) << "OnBeforeFrameSent(stream_id=" << stream_id
                 << ", type=" << int(frame_type) << ", length=" << length
                 << ", flags=" << int(flags) << ")";
  if (callbacks_->before_frame_send_callback) {
    nghttp2_frame frame;
    auto it = GetStreamInfo(stream_id);
    // The implementation of the before_frame_send_callback doesn't look at the
    // error code, so for now it's populated with 0.
    PopulateFrame(frame, frame_type, stream_id, length, flags, /*error_code=*/0,
                  it->second.before_sent_headers);
    it->second.before_sent_headers = true;
    return callbacks_->before_frame_send_callback(nullptr, &frame, user_data_);
  }
  return 0;
}

int CallbackVisitor::OnFrameSent(uint8_t frame_type, Http2StreamId stream_id,
                                 size_t length, uint8_t flags,
                                 uint32_t error_code) {
  QUICHE_VLOG(1) << "OnFrameSent(stream_id=" << stream_id
                 << ", type=" << int(frame_type) << ", length=" << length
                 << ", flags=" << int(flags) << ", error_code=" << error_code
                 << ")";
  if (callbacks_->on_frame_send_callback) {
    nghttp2_frame frame;
    auto it = GetStreamInfo(stream_id);
    PopulateFrame(frame, frame_type, stream_id, length, flags, error_code,
                  it->second.sent_headers);
    it->second.sent_headers = true;
    return callbacks_->on_frame_send_callback(nullptr, &frame, user_data_);
  }
  return 0;
}

bool CallbackVisitor::OnInvalidFrame(Http2StreamId stream_id,
                                     InvalidFrameError error) {
  QUICHE_VLOG(1) << "OnInvalidFrame(" << stream_id << ", "
                 << InvalidFrameErrorToString(error) << ")";
  QUICHE_DCHECK_EQ(stream_id, current_frame_.hd.stream_id);
  if (callbacks_->on_invalid_frame_recv_callback) {
    return 0 ==
           callbacks_->on_invalid_frame_recv_callback(
               nullptr, &current_frame_, ToNgHttp2ErrorCode(error), user_data_);
  }
  return true;
}

void CallbackVisitor::OnBeginMetadataForStream(Http2StreamId stream_id,
                                               size_t payload_length) {
  QUICHE_VLOG(1) << "OnBeginMetadataForStream(stream_id=" << stream_id
                 << ", payload_length=" << payload_length << ")";
}

bool CallbackVisitor::OnMetadataForStream(Http2StreamId stream_id,
                                          absl::string_view metadata) {
  QUICHE_VLOG(1) << "OnMetadataForStream(stream_id=" << stream_id
                 << ", len=" << metadata.size() << ")";
  if (callbacks_->on_extension_chunk_recv_callback) {
    int result = callbacks_->on_extension_chunk_recv_callback(
        nullptr, &current_frame_.hd, ToUint8Ptr(metadata.data()),
        metadata.size(), user_data_);
    return result == 0;
  }
  return true;
}

bool CallbackVisitor::OnMetadataEndForStream(Http2StreamId stream_id) {
  QUICHE_LOG_IF(DFATAL, current_frame_.hd.flags != kMetadataEndFlag);
  QUICHE_VLOG(1) << "OnMetadataEndForStream(stream_id=" << stream_id << ")";
  if (callbacks_->unpack_extension_callback) {
    void* payload;
    int result = callbacks_->unpack_extension_callback(
        nullptr, &payload, &current_frame_.hd, user_data_);
    if (result == 0 && callbacks_->on_frame_recv_callback) {
      current_frame_.ext.payload = payload;
      result = callbacks_->on_frame_recv_callback(nullptr, &current_frame_,
                                                  user_data_);
    }
    return (result == 0);
  }
  return true;
}

void CallbackVisitor::OnErrorDebug(absl::string_view message) {
  QUICHE_VLOG(1) << "OnErrorDebug(message=[" << absl::CEscape(message) << "])";
  if (callbacks_->error_callback2) {
    callbacks_->error_callback2(nullptr, -1, message.data(), message.size(),
                                user_data_);
  }
}

CallbackVisitor::StreamInfoMap::iterator CallbackVisitor::GetStreamInfo(
    Http2StreamId stream_id) {
  auto it = stream_map_.find(stream_id);
  if (it == stream_map_.end()) {
    auto p = stream_map_.insert({stream_id, {}});
    it = p.first;
  }
  return it;
}

}  // namespace adapter
}  // namespace http2
