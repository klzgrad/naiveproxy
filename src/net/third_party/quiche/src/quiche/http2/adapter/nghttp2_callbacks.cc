#include "quiche/http2/adapter/nghttp2_callbacks.h"

#include <cstdint>
#include <cstring>

#include "absl/strings/string_view.h"
#include "quiche/http2/adapter/data_source.h"
#include "quiche/http2/adapter/http2_protocol.h"
#include "quiche/http2/adapter/http2_visitor_interface.h"
#include "quiche/http2/adapter/nghttp2_data_provider.h"
#include "quiche/http2/adapter/nghttp2_util.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_endian.h"

namespace http2 {
namespace adapter {
namespace callbacks {

ssize_t OnReadyToSend(nghttp2_session* /* session */, const uint8_t* data,
                      size_t length, int flags, void* user_data) {
  QUICHE_CHECK_NE(user_data, nullptr);
  auto* visitor = static_cast<Http2VisitorInterface*>(user_data);
  const int64_t result = visitor->OnReadyToSend(ToStringView(data, length));
  QUICHE_VLOG(1) << "callbacks::OnReadyToSend(length=" << length
                 << ", flags=" << flags << ") returning " << result;
  if (result > 0) {
    return result;
  } else if (result == Http2VisitorInterface::kSendBlocked) {
    return -504;  // NGHTTP2_ERR_WOULDBLOCK
  } else {
    return -902;  // NGHTTP2_ERR_CALLBACK_FAILURE
  }
}

int OnBeginFrame(nghttp2_session* /* session */, const nghttp2_frame_hd* header,
                 void* user_data) {
  QUICHE_VLOG(1) << "callbacks::OnBeginFrame(stream_id=" << header->stream_id
                 << ", type=" << int(header->type)
                 << ", length=" << header->length
                 << ", flags=" << int(header->flags) << ")";
  QUICHE_CHECK_NE(user_data, nullptr);
  auto* visitor = static_cast<Http2VisitorInterface*>(user_data);
  bool result = visitor->OnFrameHeader(header->stream_id, header->length,
                                       header->type, header->flags);
  if (!result) {
    return NGHTTP2_ERR_CALLBACK_FAILURE;
  }
  if (header->type == NGHTTP2_DATA) {
    result = visitor->OnBeginDataForStream(header->stream_id, header->length);
  } else if (header->type == kMetadataFrameType) {
    visitor->OnBeginMetadataForStream(header->stream_id, header->length);
  }
  return result ? 0 : NGHTTP2_ERR_CALLBACK_FAILURE;
}

int OnFrameReceived(nghttp2_session* /* session */, const nghttp2_frame* frame,
                    void* user_data) {
  QUICHE_VLOG(1) << "callbacks::OnFrameReceived(stream_id="
                 << frame->hd.stream_id << ", type=" << int(frame->hd.type)
                 << ", length=" << frame->hd.length
                 << ", flags=" << int(frame->hd.flags) << ")";
  QUICHE_CHECK_NE(user_data, nullptr);
  auto* visitor = static_cast<Http2VisitorInterface*>(user_data);
  const Http2StreamId stream_id = frame->hd.stream_id;
  switch (frame->hd.type) {
    // The beginning of the DATA frame is handled in OnBeginFrame(), and the
    // beginning of the header block is handled in client/server-specific
    // callbacks. This callback handles the point at which the entire logical
    // frame has been received and processed.
    case NGHTTP2_DATA:
      if ((frame->hd.flags & NGHTTP2_FLAG_PADDED) != 0) {
        visitor->OnDataPaddingLength(stream_id, frame->data.padlen);
      }
      if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
        const bool result = visitor->OnEndStream(stream_id);
        if (!result) {
          return NGHTTP2_ERR_CALLBACK_FAILURE;
        }
      }
      break;
    case NGHTTP2_HEADERS: {
      if (frame->hd.flags & NGHTTP2_FLAG_END_HEADERS) {
        const bool result = visitor->OnEndHeadersForStream(stream_id);
        if (!result) {
          return NGHTTP2_ERR_CALLBACK_FAILURE;
        }
      }
      if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
        const bool result = visitor->OnEndStream(stream_id);
        if (!result) {
          return NGHTTP2_ERR_CALLBACK_FAILURE;
        }
      }
      break;
    }
    case NGHTTP2_PRIORITY: {
      nghttp2_priority_spec priority_spec = frame->priority.pri_spec;
      visitor->OnPriorityForStream(stream_id, priority_spec.stream_id,
                                   priority_spec.weight,
                                   priority_spec.exclusive != 0);
      break;
    }
    case NGHTTP2_RST_STREAM: {
      visitor->OnRstStream(stream_id,
                           ToHttp2ErrorCode(frame->rst_stream.error_code));
      break;
    }
    case NGHTTP2_SETTINGS:
      if (frame->hd.flags & NGHTTP2_FLAG_ACK) {
        visitor->OnSettingsAck();
      } else {
        visitor->OnSettingsStart();
        for (size_t i = 0; i < frame->settings.niv; ++i) {
          nghttp2_settings_entry entry = frame->settings.iv[i];
          // The nghttp2_settings_entry uses int32_t for the ID; we must cast.
          visitor->OnSetting(Http2Setting{
              static_cast<Http2SettingsId>(entry.settings_id), entry.value});
        }
        visitor->OnSettingsEnd();
      }
      break;
    case NGHTTP2_PUSH_PROMISE:
      // This case is handled by headers-related callbacks:
      //   1. visitor->OnPushPromiseForStream() is invoked in the client-side
      //      OnHeadersStart() adapter callback, as nghttp2 only allows clients
      //      to receive PUSH_PROMISE frames.
      //   2. visitor->OnHeaderForStream() is invoked for each server push
      //      request header in the PUSH_PROMISE header block.
      //   3. This switch statement is reached once all server push request
      //      headers have been parsed.
      break;
    case NGHTTP2_PING: {
      Http2PingId ping_id;
      std::memcpy(&ping_id, frame->ping.opaque_data, sizeof(Http2PingId));
      visitor->OnPing(quiche::QuicheEndian::NetToHost64(ping_id),
                      (frame->hd.flags & NGHTTP2_FLAG_ACK) != 0);
      break;
    }
    case NGHTTP2_GOAWAY: {
      absl::string_view opaque_data(
          reinterpret_cast<const char*>(frame->goaway.opaque_data),
          frame->goaway.opaque_data_len);
      const bool result = visitor->OnGoAway(
          frame->goaway.last_stream_id,
          ToHttp2ErrorCode(frame->goaway.error_code), opaque_data);
      if (!result) {
        return NGHTTP2_ERR_CALLBACK_FAILURE;
      }
      break;
    }
    case NGHTTP2_WINDOW_UPDATE: {
      visitor->OnWindowUpdate(stream_id,
                              frame->window_update.window_size_increment);
      break;
    }
    case NGHTTP2_CONTINUATION:
      // This frame type should not be passed to any callbacks, according to
      // https://nghttp2.org/documentation/enums.html#c.NGHTTP2_CONTINUATION.
      QUICHE_LOG(ERROR) << "Unexpected receipt of NGHTTP2_CONTINUATION type!";
      break;
    case NGHTTP2_ALTSVC:
      break;
    case NGHTTP2_ORIGIN:
      break;
  }

  return 0;
}

int OnBeginHeaders(nghttp2_session* /* session */, const nghttp2_frame* frame,
                   void* user_data) {
  QUICHE_VLOG(1) << "callbacks::OnBeginHeaders(stream_id="
                 << frame->hd.stream_id << ")";
  QUICHE_CHECK_NE(user_data, nullptr);
  auto* visitor = static_cast<Http2VisitorInterface*>(user_data);
  const bool result = visitor->OnBeginHeadersForStream(frame->hd.stream_id);
  return result ? 0 : NGHTTP2_ERR_CALLBACK_FAILURE;
}

int OnHeader(nghttp2_session* /* session */, const nghttp2_frame* frame,
             nghttp2_rcbuf* name, nghttp2_rcbuf* value, uint8_t /*flags*/,
             void* user_data) {
  QUICHE_VLOG(2) << "callbacks::OnHeader(stream_id=" << frame->hd.stream_id
                 << ", name=[" << absl::CEscape(ToStringView(name))
                 << "], value=[" << absl::CEscape(ToStringView(value)) << "])";
  QUICHE_CHECK_NE(user_data, nullptr);
  auto* visitor = static_cast<Http2VisitorInterface*>(user_data);
  const Http2VisitorInterface::OnHeaderResult result =
      visitor->OnHeaderForStream(frame->hd.stream_id, ToStringView(name),
                                 ToStringView(value));
  switch (result) {
    case Http2VisitorInterface::HEADER_OK:
      return 0;
    case Http2VisitorInterface::HEADER_CONNECTION_ERROR:
    case Http2VisitorInterface::HEADER_COMPRESSION_ERROR:
      return NGHTTP2_ERR_CALLBACK_FAILURE;
    case Http2VisitorInterface::HEADER_RST_STREAM:
    case Http2VisitorInterface::HEADER_FIELD_INVALID:
    case Http2VisitorInterface::HEADER_HTTP_MESSAGING:
      return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
  }
  // Unexpected value.
  return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
}

int OnBeforeFrameSent(nghttp2_session* /* session */,
                      const nghttp2_frame* frame, void* user_data) {
  QUICHE_VLOG(1) << "callbacks::OnBeforeFrameSent(stream_id="
                 << frame->hd.stream_id << ", type=" << int(frame->hd.type)
                 << ", length=" << frame->hd.length
                 << ", flags=" << int(frame->hd.flags) << ")";
  QUICHE_CHECK_NE(user_data, nullptr);
  LogBeforeSend(*frame);
  auto* visitor = static_cast<Http2VisitorInterface*>(user_data);
  return visitor->OnBeforeFrameSent(frame->hd.type, frame->hd.stream_id,
                                    frame->hd.length, frame->hd.flags);
}

int OnFrameSent(nghttp2_session* /* session */, const nghttp2_frame* frame,
                void* user_data) {
  QUICHE_VLOG(1) << "callbacks::OnFrameSent(stream_id=" << frame->hd.stream_id
                 << ", type=" << int(frame->hd.type)
                 << ", length=" << frame->hd.length
                 << ", flags=" << int(frame->hd.flags) << ")";
  QUICHE_CHECK_NE(user_data, nullptr);
  auto* visitor = static_cast<Http2VisitorInterface*>(user_data);
  uint32_t error_code = 0;
  if (frame->hd.type == NGHTTP2_RST_STREAM) {
    error_code = frame->rst_stream.error_code;
  } else if (frame->hd.type == NGHTTP2_GOAWAY) {
    error_code = frame->goaway.error_code;
  }
  return visitor->OnFrameSent(frame->hd.type, frame->hd.stream_id,
                              frame->hd.length, frame->hd.flags, error_code);
}

int OnFrameNotSent(nghttp2_session* /* session */, const nghttp2_frame* frame,
                   int /* lib_error_code */, void* /* user_data */) {
  QUICHE_VLOG(1) << "callbacks::OnFrameNotSent(stream_id="
                 << frame->hd.stream_id << ", type=" << int(frame->hd.type)
                 << ", length=" << frame->hd.length
                 << ", flags=" << int(frame->hd.flags) << ")";
  if (frame->hd.type == kMetadataFrameType) {
    auto* source = static_cast<MetadataSource*>(frame->ext.payload);
    if (source == nullptr) {
      QUICHE_BUG(not_sent_payload_is_nullptr)
          << "Extension frame payload for stream " << frame->hd.stream_id
          << " is null!";
    } else {
      source->OnFailure();
    }
  }
  return 0;
}

int OnInvalidFrameReceived(nghttp2_session* /* session */,
                           const nghttp2_frame* frame, int lib_error_code,
                           void* user_data) {
  QUICHE_VLOG(1) << "callbacks::OnInvalidFrameReceived(stream_id="
                 << frame->hd.stream_id << ", InvalidFrameError="
                 << int(ToInvalidFrameError(lib_error_code)) << ")";
  QUICHE_CHECK_NE(user_data, nullptr);
  auto* visitor = static_cast<Http2VisitorInterface*>(user_data);
  const bool result = visitor->OnInvalidFrame(
      frame->hd.stream_id, ToInvalidFrameError(lib_error_code));
  return result ? 0 : NGHTTP2_ERR_CALLBACK_FAILURE;
}

int OnDataChunk(nghttp2_session* /* session */, uint8_t /*flags*/,
                Http2StreamId stream_id, const uint8_t* data, size_t len,
                void* user_data) {
  QUICHE_VLOG(1) << "callbacks::OnDataChunk(stream_id=" << stream_id
                 << ", length=" << len << ")";
  QUICHE_CHECK_NE(user_data, nullptr);
  auto* visitor = static_cast<Http2VisitorInterface*>(user_data);
  const bool result = visitor->OnDataForStream(
      stream_id, absl::string_view(reinterpret_cast<const char*>(data), len));
  return result ? 0 : NGHTTP2_ERR_CALLBACK_FAILURE;
}

int OnStreamClosed(nghttp2_session* /* session */, Http2StreamId stream_id,
                   uint32_t error_code, void* user_data) {
  QUICHE_VLOG(1) << "callbacks::OnStreamClosed(stream_id=" << stream_id
                 << ", error_code=" << error_code << ")";
  QUICHE_CHECK_NE(user_data, nullptr);
  auto* visitor = static_cast<Http2VisitorInterface*>(user_data);
  const bool result =
      visitor->OnCloseStream(stream_id, ToHttp2ErrorCode(error_code));
  return result ? 0 : NGHTTP2_ERR_CALLBACK_FAILURE;
}

int OnExtensionChunkReceived(nghttp2_session* /*session*/,
                             const nghttp2_frame_hd* hd, const uint8_t* data,
                             size_t len, void* user_data) {
  QUICHE_CHECK_NE(user_data, nullptr);
  auto* visitor = static_cast<Http2VisitorInterface*>(user_data);
  if (hd->type != kMetadataFrameType) {
    QUICHE_LOG(ERROR) << "Unexpected frame type: "
                      << static_cast<int>(hd->type);
    return NGHTTP2_ERR_CANCEL;
  }
  const bool result =
      visitor->OnMetadataForStream(hd->stream_id, ToStringView(data, len));
  return result ? 0 : NGHTTP2_ERR_CALLBACK_FAILURE;
}

int OnUnpackExtensionCallback(nghttp2_session* /*session*/, void** /*payload*/,
                              const nghttp2_frame_hd* hd, void* user_data) {
  QUICHE_CHECK_NE(user_data, nullptr);
  auto* visitor = static_cast<Http2VisitorInterface*>(user_data);
  if (hd->flags == kMetadataEndFlag) {
    const bool result = visitor->OnMetadataEndForStream(hd->stream_id);
    if (!result) {
      return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
  }
  return 0;
}

ssize_t OnPackExtensionCallback(nghttp2_session* /*session*/, uint8_t* buf,
                                size_t len, const nghttp2_frame* frame,
                                void* user_data) {
  QUICHE_CHECK_NE(user_data, nullptr);
  auto* source = static_cast<MetadataSource*>(frame->ext.payload);
  if (source == nullptr) {
    QUICHE_BUG(payload_is_nullptr) << "Extension frame payload for stream "
                                   << frame->hd.stream_id << " is null!";
    return NGHTTP2_ERR_CALLBACK_FAILURE;
  }
  const std::pair<int64_t, bool> result = source->Pack(buf, len);
  if (result.first < 0) {
    return NGHTTP2_ERR_CALLBACK_FAILURE;
  }
  const bool end_metadata_flag = (frame->hd.flags & kMetadataEndFlag);
  QUICHE_LOG_IF(DFATAL, result.second != end_metadata_flag)
      << "Metadata ends: " << result.second
      << " has kMetadataEndFlag: " << end_metadata_flag;
  return result.first;
}

int OnError(nghttp2_session* /*session*/, int /*lib_error_code*/,
            const char* msg, size_t len, void* user_data) {
  QUICHE_VLOG(1) << "callbacks::OnError(" << absl::string_view(msg, len) << ")";
  QUICHE_CHECK_NE(user_data, nullptr);
  auto* visitor = static_cast<Http2VisitorInterface*>(user_data);
  visitor->OnErrorDebug(absl::string_view(msg, len));
  return 0;
}

nghttp2_session_callbacks_unique_ptr Create() {
  nghttp2_session_callbacks* callbacks;
  nghttp2_session_callbacks_new(&callbacks);

  nghttp2_session_callbacks_set_send_callback(callbacks, &OnReadyToSend);
  nghttp2_session_callbacks_set_on_begin_frame_callback(callbacks,
                                                        &OnBeginFrame);
  nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks,
                                                       &OnFrameReceived);
  nghttp2_session_callbacks_set_on_begin_headers_callback(callbacks,
                                                          &OnBeginHeaders);
  nghttp2_session_callbacks_set_on_header_callback2(callbacks, &OnHeader);
  nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks,
                                                            &OnDataChunk);
  nghttp2_session_callbacks_set_on_stream_close_callback(callbacks,
                                                         &OnStreamClosed);
  nghttp2_session_callbacks_set_before_frame_send_callback(callbacks,
                                                           &OnBeforeFrameSent);
  nghttp2_session_callbacks_set_on_frame_send_callback(callbacks, &OnFrameSent);
  nghttp2_session_callbacks_set_on_frame_not_send_callback(callbacks,
                                                           &OnFrameNotSent);
  nghttp2_session_callbacks_set_on_invalid_frame_recv_callback(
      callbacks, &OnInvalidFrameReceived);
  nghttp2_session_callbacks_set_error_callback2(callbacks, &OnError);
  nghttp2_session_callbacks_set_send_data_callback(
      callbacks, &DataFrameSourceSendCallback);
  nghttp2_session_callbacks_set_pack_extension_callback(
      callbacks, &OnPackExtensionCallback);
  nghttp2_session_callbacks_set_unpack_extension_callback(
      callbacks, &OnUnpackExtensionCallback);
  nghttp2_session_callbacks_set_on_extension_chunk_recv_callback(
      callbacks, &OnExtensionChunkReceived);
  return MakeCallbacksPtr(callbacks);
}

}  // namespace callbacks
}  // namespace adapter
}  // namespace http2
