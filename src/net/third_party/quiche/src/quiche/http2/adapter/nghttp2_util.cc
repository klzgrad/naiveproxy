#include "quiche/http2/adapter/nghttp2_util.h"

#include <cstdint>
#include <memory>

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
    const spdy::Http2HeaderBlock& headers, absl::string_view response_code) {
  // Allocate enough for all headers and also the :status pseudoheader.
  const int num_headers = headers.size();
  std::vector<nghttp2_nv> nghttp2_nvs;
  nghttp2_nvs.reserve(num_headers + 1);

  // Add the :status pseudoheader first.
  nghttp2_nv status;
  status.name = ToUint8Ptr(kHttp2StatusPseudoHeader);
  status.namelen = strlen(kHttp2StatusPseudoHeader);
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

class Nghttp2DataFrameSource : public DataFrameSource {
 public:
  Nghttp2DataFrameSource(nghttp2_data_provider provider,
                         nghttp2_send_data_callback send_data, void* user_data)
      : provider_(std::move(provider)),
        send_data_(std::move(send_data)),
        user_data_(user_data) {}

  std::pair<int64_t, bool> SelectPayloadLength(size_t max_length) override {
    const int32_t stream_id = 0;
    uint32_t data_flags = 0;
    int64_t result = provider_.read_callback(
        nullptr /* session */, stream_id, nullptr /* buf */, max_length,
        &data_flags, &provider_.source, nullptr /* user_data */);
    if (result == NGHTTP2_ERR_DEFERRED) {
      return {kBlocked, false};
    } else if (result < 0) {
      return {kError, false};
    } else if ((data_flags & NGHTTP2_DATA_FLAG_NO_COPY) == 0) {
      QUICHE_LOG(ERROR) << "Source did not use the zero-copy API!";
      return {kError, false};
    } else {
      const bool eof = data_flags & NGHTTP2_DATA_FLAG_EOF;
      if (eof && (data_flags & NGHTTP2_DATA_FLAG_NO_END_STREAM) == 0) {
        send_fin_ = true;
      }
      return {result, eof};
    }
  }

  bool Send(absl::string_view frame_header, size_t payload_length) override {
    nghttp2_frame frame;
    frame.hd.type = 0;
    frame.hd.length = payload_length;
    frame.hd.flags = 0;
    frame.hd.stream_id = 0;
    frame.data.padlen = 0;
    const int result = send_data_(
        nullptr /* session */, &frame, ToUint8Ptr(frame_header.data()),
        payload_length, &provider_.source, user_data_);
    QUICHE_LOG_IF(ERROR, result < 0 && result != NGHTTP2_ERR_WOULDBLOCK)
        << "Unexpected error code from send: " << result;
    return result == 0;
  }

  bool send_fin() const override { return send_fin_; }

 private:
  nghttp2_data_provider provider_;
  nghttp2_send_data_callback send_data_;
  void* user_data_;
  bool send_fin_ = false;
};

std::unique_ptr<DataFrameSource> MakeZeroCopyDataFrameSource(
    nghttp2_data_provider provider, void* user_data,
    nghttp2_send_data_callback send_data) {
  return std::make_unique<Nghttp2DataFrameSource>(
      std::move(provider), std::move(send_data), user_data);
}

absl::string_view ErrorString(uint32_t error_code) {
  return Http2ErrorCodeToString(static_cast<Http2ErrorCode>(error_code));
}

size_t PaddingLength(uint8_t flags, size_t padlen) {
  return (flags & 0x8 ? 1 : 0) + padlen;
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
                           << " entries, is_ack: " << (frame.hd.flags & 0x01);
      break;
    case FrameType::PUSH_PROMISE:
      HTTP2_FRAME_SEND_LOG << "Sending PUSH_PROMISE";
      break;
    case FrameType::PING: {
      Http2PingId ping_id;
      std::memcpy(&ping_id, frame.ping.opaque_data, sizeof(Http2PingId));
      HTTP2_FRAME_SEND_LOG << "Sending PING with unique_id "
                           << quiche::QuicheEndian::NetToHost64(ping_id)
                           << ", is_ack: " << (frame.hd.flags & 0x01);
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
