#include "http2/adapter/nghttp2_callbacks.h"

#include <cstdint>
#include <cstring>

#include "absl/strings/string_view.h"
#include "http2/adapter/http2_protocol.h"
#include "http2/adapter/http2_visitor_interface.h"
#include "http2/adapter/nghttp2_util.h"
#include "third_party/nghttp2/nghttp2.h"
#include "third_party/nghttp2/src/lib/includes/nghttp2/nghttp2.h"
#include "common/platform/api/quiche_logging.h"
#include "common/quiche_endian.h"

namespace http2 {
namespace adapter {

int OnBeginFrame(nghttp2_session* /* session */,
                 const nghttp2_frame_hd* header,
                 void* user_data) {
  auto* visitor = static_cast<Http2VisitorInterface*>(user_data);
  if (header->type == NGHTTP2_DATA) {
    visitor->OnBeginDataForStream(header->stream_id, header->length);
  }
  return 0;
}

int OnFrameReceived(nghttp2_session* /* session */,
                    const nghttp2_frame* frame,
                    void* user_data) {
  auto* visitor = static_cast<Http2VisitorInterface*>(user_data);
  const Http2StreamId stream_id = frame->hd.stream_id;
  switch (frame->hd.type) {
    // The beginning of the DATA frame is handled in OnBeginFrame(), and the
    // beginning of the header block is handled in client/server-specific
    // callbacks. This callback handles the point at which the entire logical
    // frame has been received and processed.
    case NGHTTP2_DATA:
      if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
        visitor->OnEndStream(stream_id);
      }
      break;
    case NGHTTP2_HEADERS: {
      if (frame->hd.flags & NGHTTP2_FLAG_END_HEADERS) {
        visitor->OnEndHeadersForStream(stream_id);
      }
      if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
        visitor->OnEndStream(stream_id);
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
        for (int i = 0; i < frame->settings.niv; ++i) {
          nghttp2_settings_entry entry = frame->settings.iv[i];
          // The nghttp2_settings_entry uses int32_t for the ID; we must cast.
          visitor->OnSetting(Http2Setting{
              .id = static_cast<Http2SettingsId>(entry.settings_id),
              .value = entry.value});
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
      visitor->OnGoAway(frame->goaway.last_stream_id,
                        ToHttp2ErrorCode(frame->goaway.error_code),
                        opaque_data);
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

int OnBeginHeaders(nghttp2_session* /* session */,
                   const nghttp2_frame* frame,
                   void* user_data) {
  auto* visitor = static_cast<Http2VisitorInterface*>(user_data);
  visitor->OnBeginHeadersForStream(frame->hd.stream_id);
  return 0;
}

int OnHeader(nghttp2_session* /* session */,
             const nghttp2_frame* frame,
             nghttp2_rcbuf* name,
             nghttp2_rcbuf* value,
             uint8_t flags,
             void* user_data) {
  auto* visitor = static_cast<Http2VisitorInterface*>(user_data);
  visitor->OnHeaderForStream(frame->hd.stream_id, ToStringView(name),
                             ToStringView(value));
  return 0;
}

int OnDataChunk(nghttp2_session* /* session */,
                uint8_t flags,
                Http2StreamId stream_id,
                const uint8_t* data,
                size_t len,
                void* user_data) {
  auto* visitor = static_cast<Http2VisitorInterface*>(user_data);
  visitor->OnDataForStream(
      stream_id, absl::string_view(reinterpret_cast<const char*>(data), len));
  return 0;
}

int OnStreamClosed(nghttp2_session* /* session */,
                   Http2StreamId stream_id,
                   uint32_t error_code,
                   void* user_data) {
  auto* visitor = static_cast<Http2VisitorInterface*>(user_data);
  if (error_code == static_cast<uint32_t>(Http2ErrorCode::NO_ERROR)) {
    visitor->OnCloseStream(stream_id);
  } else {
    visitor->OnAbortStream(stream_id, ToHttp2ErrorCode(error_code));
  }
  return 0;
}

ssize_t OnReadyToReadDataForStream(nghttp2_session* /* session */,
                                   Http2StreamId stream_id,
                                   uint8_t* dest_buffer,
                                   size_t max_length,
                                   uint32_t* data_flags,
                                   nghttp2_data_source* source,
                                   void* user_data) {
  auto* visitor = static_cast<Http2VisitorInterface*>(source->ptr);
  ssize_t bytes_to_send = 0;
  bool end_stream = false;
  visitor->OnReadyToSendDataForStream(stream_id,
                                      reinterpret_cast<char*>(dest_buffer),
                                      max_length, &bytes_to_send, &end_stream);
  if (bytes_to_send >= 0 && end_stream) {
    *data_flags |= NGHTTP2_DATA_FLAG_EOF;
  }
  return bytes_to_send;
}

}  // namespace adapter
}  // namespace http2
