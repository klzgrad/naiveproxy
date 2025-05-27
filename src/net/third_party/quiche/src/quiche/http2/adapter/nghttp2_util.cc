#include "quiche/http2/adapter/nghttp2_util.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/http2/adapter/http2_protocol.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_endian.h"

namespace http2 {
namespace adapter {

namespace {

using InvalidFrameError = Http2VisitorInterface::InvalidFrameError;

void DeleteCallbacks(nghttp2_session_callbacks* callbacks) {
  if (callbacks) {
    nghttp2_session_callbacks_del(callbacks);
  }
}

void DeleteSession(nghttp2_session* session) {
  if (session) {
    nghttp2_session_del(session);
  }
}

}  // namespace

nghttp2_session_callbacks_unique_ptr MakeCallbacksPtr(
    nghttp2_session_callbacks* callbacks) {
  return nghttp2_session_callbacks_unique_ptr(callbacks, &DeleteCallbacks);
}

nghttp2_session_unique_ptr MakeSessionPtr(nghttp2_session* session) {
  return nghttp2_session_unique_ptr(session, &DeleteSession);
}

uint8_t* ToUint8Ptr(char* str) { return reinterpret_cast<uint8_t*>(str); }
uint8_t* ToUint8Ptr(const char* str) {
  return const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(str));
}

absl::string_view ToStringView(nghttp2_rcbuf* rc_buffer) {
  nghttp2_vec buffer = nghttp2_rcbuf_get_buf(rc_buffer);
  return absl::string_view(reinterpret_cast<const char*>(buffer.base),
                           buffer.len);
}

absl::string_view ToStringView(uint8_t* pointer, size_t length) {
  return absl::string_view(reinterpret_cast<const char*>(pointer), length);
}

absl::string_view ToStringView(const uint8_t* pointer, size_t length) {
  return absl::string_view(reinterpret_cast<const char*>(pointer), length);
}

std::vector<nghttp2_nv> GetNghttp2Nvs(absl::Span<const Header> headers) {
  const int num_headers = headers.size();
  std::vector<nghttp2_nv> nghttp2_nvs;
  nghttp2_nvs.reserve(num_headers);
  for (int i = 0; i < num_headers; ++i) {
    nghttp2_nv header;
    uint8_t flags = NGHTTP2_NV_FLAG_NONE;

    const auto [name, no_copy_name] = GetStringView(headers[i].first);
    header.name = ToUint8Ptr(name.data());
    header.namelen = name.size();
    if (no_copy_name) {
      flags |= NGHTTP2_NV_FLAG_NO_COPY_NAME;
    }
    const auto [value, no_copy_value] = GetStringView(headers[i].second);
    header.value = ToUint8Ptr(value.data());
    header.valuelen = value.size();
    if (no_copy_value) {
      flags |= NGHTTP2_NV_FLAG_NO_COPY_VALUE;
    }
    header.flags = flags;
    nghttp2_nvs.push_back(std::move(header));
  }

  return nghttp2_nvs;
}

std::vector<nghttp2_nv> GetResponseNghttp2Nvs(
    const quiche::HttpHeaderBlock& headers, absl::string_view response_code) {
  // Allocate enough for all headers and also the :status pseudoheader.
  const int num_headers = headers.size();
  std::vector<nghttp2_nv> nghttp2_nvs;
  nghttp2_nvs.reserve(num_headers + 1);

  // Add the :status pseudoheader first.
  nghttp2_nv status;
  status.name = ToUint8Ptr(kHttp2StatusPseudoHeader.data());
  status.namelen = kHttp2StatusPseudoHeader.size();
  status.value = ToUint8Ptr(response_code.data());
  status.valuelen = response_code.size();
  status.flags = NGHTTP2_FLAG_NONE;
  nghttp2_nvs.push_back(std::move(status));

  // Add the remaining headers.
  for (const auto& header_pair : headers) {
    nghttp2_nv header;
    header.name = ToUint8Ptr(header_pair.first.data());
    header.namelen = header_pair.first.size();
    header.value = ToUint8Ptr(header_pair.second.data());
    header.valuelen = header_pair.second.size();
    header.flags = NGHTTP2_FLAG_NONE;
    nghttp2_nvs.push_back(std::move(header));
  }

  return nghttp2_nvs;
}

Http2ErrorCode ToHttp2ErrorCode(uint32_t wire_error_code) {
  if (wire_error_code > static_cast<int>(Http2ErrorCode::MAX_ERROR_CODE)) {
    return Http2ErrorCode::INTERNAL_ERROR;
  }
  return static_cast<Http2ErrorCode>(wire_error_code);
}

int ToNgHttp2ErrorCode(InvalidFrameError error) {
  switch (error) {
    case InvalidFrameError::kProtocol:
      return NGHTTP2_ERR_PROTO;
    case InvalidFrameError::kRefusedStream:
      return NGHTTP2_ERR_REFUSED_STREAM;
    case InvalidFrameError::kHttpHeader:
      return NGHTTP2_ERR_HTTP_HEADER;
    case InvalidFrameError::kHttpMessaging:
      return NGHTTP2_ERR_HTTP_MESSAGING;
    case InvalidFrameError::kFlowControl:
      return NGHTTP2_ERR_FLOW_CONTROL;
    case InvalidFrameError::kStreamClosed:
      return NGHTTP2_ERR_STREAM_CLOSED;
  }
  return NGHTTP2_ERR_PROTO;
}

InvalidFrameError ToInvalidFrameError(int error) {
  switch (error) {
    case NGHTTP2_ERR_PROTO:
      return InvalidFrameError::kProtocol;
    case NGHTTP2_ERR_REFUSED_STREAM:
      return InvalidFrameError::kRefusedStream;
    case NGHTTP2_ERR_HTTP_HEADER:
      return InvalidFrameError::kHttpHeader;
    case NGHTTP2_ERR_HTTP_MESSAGING:
      return InvalidFrameError::kHttpMessaging;
    case NGHTTP2_ERR_FLOW_CONTROL:
      return InvalidFrameError::kFlowControl;
    case NGHTTP2_ERR_STREAM_CLOSED:
      return InvalidFrameError::kStreamClosed;
  }
  return InvalidFrameError::kProtocol;
}

absl::string_view ErrorString(uint32_t error_code) {
  return Http2ErrorCodeToString(static_cast<Http2ErrorCode>(error_code));
}

size_t PaddingLength(uint8_t flags, size_t padlen) {
  return (flags & PADDED_FLAG ? 1 : 0) + padlen;
}

struct NvFormatter {
  void operator()(std::string* out, const nghttp2_nv& nv) {
    absl::StrAppend(out, ToStringView(nv.name, nv.namelen), ": ",
                    ToStringView(nv.value, nv.valuelen));
  }
};

std::string NvsAsString(nghttp2_nv* nva, size_t nvlen) {
  return absl::StrJoin(absl::MakeConstSpan(nva, nvlen), ", ", NvFormatter());
}

#define HTTP2_FRAME_SEND_LOG QUICHE_VLOG(1)

void LogBeforeSend(const nghttp2_frame& frame) {
  switch (static_cast<FrameType>(frame.hd.type)) {
    case FrameType::DATA:
      HTTP2_FRAME_SEND_LOG << "Sending DATA on stream " << frame.hd.stream_id
                           << " with length "
                           << frame.hd.length - PaddingLength(frame.hd.flags,
                                                              frame.data.padlen)
                           << " and padding "
                           << PaddingLength(frame.hd.flags, frame.data.padlen);
      break;
    case FrameType::HEADERS:
      HTTP2_FRAME_SEND_LOG << "Sending HEADERS on stream " << frame.hd.stream_id
                           << " with headers ["
                           << NvsAsString(frame.headers.nva,
                                          frame.headers.nvlen)
                           << "]";
      break;
    case FrameType::PRIORITY:
      HTTP2_FRAME_SEND_LOG << "Sending PRIORITY";
      break;
    case FrameType::RST_STREAM:
      HTTP2_FRAME_SEND_LOG << "Sending RST_STREAM on stream "
                           << frame.hd.stream_id << " with error code "
                           << ErrorString(frame.rst_stream.error_code);
      break;
    case FrameType::SETTINGS:
      HTTP2_FRAME_SEND_LOG << "Sending SETTINGS with " << frame.settings.niv
                           << " entries, is_ack: "
                           << (frame.hd.flags & ACK_FLAG);
      break;
    case FrameType::PUSH_PROMISE:
      HTTP2_FRAME_SEND_LOG << "Sending PUSH_PROMISE";
      break;
    case FrameType::PING: {
      Http2PingId ping_id;
      std::memcpy(&ping_id, frame.ping.opaque_data, sizeof(Http2PingId));
      HTTP2_FRAME_SEND_LOG << "Sending PING with unique_id "
                           << quiche::QuicheEndian::NetToHost64(ping_id)
                           << ", is_ack: " << (frame.hd.flags & ACK_FLAG);
      break;
    }
    case FrameType::GOAWAY:
      HTTP2_FRAME_SEND_LOG << "Sending GOAWAY with last_stream: "
                           << frame.goaway.last_stream_id << " and error "
                           << ErrorString(frame.goaway.error_code);
      break;
    case FrameType::WINDOW_UPDATE:
      HTTP2_FRAME_SEND_LOG << "Sending WINDOW_UPDATE on stream "
                           << frame.hd.stream_id << " with update delta "
                           << frame.window_update.window_size_increment;
      break;
    case FrameType::CONTINUATION:
      HTTP2_FRAME_SEND_LOG << "Sending CONTINUATION, which is unexpected";
      break;
  }
}

#undef HTTP2_FRAME_SEND_LOG

}  // namespace adapter
}  // namespace http2
