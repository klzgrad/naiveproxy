/*
 * Copyright (C) 2021 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "perfetto/ext/base/http/http_server.h"

#include <cinttypes>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/base64.h"
#include "perfetto/ext/base/endian.h"
#include "perfetto/ext/base/http/sha1.h"
#include "perfetto/ext/base/paged_memory.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/ext/base/unix_socket.h"

namespace perfetto::base {

namespace {
constexpr size_t kMaxPayloadSize = 64 * 1024 * 1024;
constexpr size_t kMaxRequestSize = kMaxPayloadSize + 4096;

enum WebsocketOpcode : uint8_t {
  kOpcodeContinuation = 0x0,
  kOpcodeText = 0x1,
  kOpcodeBinary = 0x2,
  kOpcodeDataUnused = 0x3,
  kOpcodeClose = 0x8,
  kOpcodePing = 0x9,
  kOpcodePong = 0xA,
  kOpcodeControlUnused = 0xB,
};

// From https://datatracker.ietf.org/doc/html/rfc6455#section-1.3.
constexpr char kWebsocketGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

}  // namespace

HttpServer::HttpServer(TaskRunner* task_runner, HttpRequestHandler* req_handler)
    : task_runner_(task_runner), req_handler_(req_handler) {}
HttpServer::~HttpServer() = default;

void HttpServer::ListenOnIpV4(const std::string& ip_addr) {
  PERFETTO_LOG("[HTTP] Starting HTTP server on %s", ip_addr.c_str());
  sock4_ = UnixSocket::Listen(ip_addr, this, task_runner_, SockFamily::kInet,
                              SockType::kStream);
  bool ipv4_listening = sock4_ && sock4_->is_listening();
  if (!ipv4_listening) {
    PERFETTO_PLOG("Failed to listen on IPv4 socket: \"%s\"", ip_addr.c_str());
    sock4_.reset();
  }
}

void HttpServer::ListenOnIpV6(const std::string& ip_addr) {
  PERFETTO_LOG("[HTTP] Starting HTTP server on %s", ip_addr.c_str());
  sock6_ = UnixSocket::Listen(ip_addr, this, task_runner_, SockFamily::kInet6,
                              SockType::kStream);
  bool ipv6_listening = sock6_ && sock6_->is_listening();
  if (!ipv6_listening) {
    PERFETTO_PLOG("Failed to listen on IPv6 socket: \"%s\"", ip_addr.c_str());
    sock6_.reset();
  }
}

void HttpServer::Start(const std::string& listen_ip, int port) {
  // On some poorly configured machines, localhost does *not* resolve to both
  // [::1] even though IPv6 is present. On such machines, we can end up in a
  // situation where the client expects us to use IPv6 (as inside G3, we
  // unconditionally use IPv6) even though we are not binding to [::1] because
  // getaddrinfo does not return it.
  //
  // Work around this by always binding to both regardless of what getaddrinfo
  // returns.
  if (listen_ip == "localhost") {
    ListenOnIpV4("127.0.0.1:" + std::to_string(port));
    ListenOnIpV6("[::1]:" + std::to_string(port));
    return;
  }
  std::string port_str = std::to_string(port);
  std::vector<NetAddrInfo> addr_infos = GetNetAddrInfo(listen_ip, port_str);
  for (NetAddrInfo& info : addr_infos) {
    if (info.family == SockFamily::kInet) {
      ListenOnIpV4(info.ip_port);
    } else if (info.family == SockFamily::kInet6) {
      ListenOnIpV6(info.ip_port);
    }
  }
}

void HttpServer::AddAllowedOrigin(const std::string& origin) {
  allowed_origins_.emplace_back(origin);
}

void HttpServer::OnNewIncomingConnection(
    UnixSocket*,  // The listening socket, irrelevant here.
    std::unique_ptr<UnixSocket> sock) {
  PERFETTO_LOG("[HTTP] New connection");
  clients_.emplace_back(std::move(sock));
}

void HttpServer::OnConnect(UnixSocket*, bool) {}

void HttpServer::OnDisconnect(UnixSocket* sock) {
  PERFETTO_LOG("[HTTP] Client disconnected");
  for (auto it = clients_.begin(); it != clients_.end(); ++it) {
    if (it->sock.get() == sock) {
      req_handler_->OnHttpConnectionClosed(&*it);
      clients_.erase(it);
      return;
    }
  }
  PERFETTO_DFATAL("[HTTP] Untracked client in OnDisconnect()");
}

void HttpServer::OnDataAvailable(UnixSocket* sock) {
  HttpServerConnection* conn = nullptr;
  for (auto it = clients_.begin(); it != clients_.end() && !conn; ++it)
    conn = (it->sock.get() == sock) ? &*it : nullptr;
  PERFETTO_CHECK(conn);

  char* rxbuf = reinterpret_cast<char*>(conn->rxbuf.Get());
  for (;;) {
    size_t avail = conn->rxbuf_avail();
    PERFETTO_CHECK(avail <= kMaxRequestSize);
    if (avail == 0) {
      conn->SendResponseAndClose("413 Payload Too Large");
      return;
    }
    size_t rsize = sock->Receive(&rxbuf[conn->rxbuf_used], avail);
    conn->rxbuf_used += rsize;
    if (rsize == 0 || conn->rxbuf_avail() == 0)
      break;
  }

  // At this point |rxbuf| can contain a partial HTTP request, a full one or
  // more (in case of HTTP Keepalive pipelining).
  for (;;) {
    size_t bytes_consumed;

    if (conn->is_websocket()) {
      bytes_consumed = ParseOneWebsocketFrame(conn);
    } else {
      bytes_consumed = ParseOneHttpRequest(conn);
    }

    if (bytes_consumed == 0)
      break;
    memmove(rxbuf, &rxbuf[bytes_consumed], conn->rxbuf_used - bytes_consumed);
    conn->rxbuf_used -= bytes_consumed;
  }
}

// Parses the HTTP request and invokes HandleRequest(). It returns the size of
// the HTTP header + body that has been processed or 0 if there isn't enough
// data for a full HTTP request in the buffer.
size_t HttpServer::ParseOneHttpRequest(HttpServerConnection* conn) {
  auto* rxbuf = reinterpret_cast<char*>(conn->rxbuf.Get());
  StringView buf_view(rxbuf, conn->rxbuf_used);
  bool has_parsed_first_line = false;
  bool all_headers_received = false;
  HttpRequest http_req(conn);
  size_t body_size = 0;

  // This loop parses the HTTP request headers and sets the |body_offset|.
  while (!buf_view.empty()) {
    size_t next = buf_view.find('\n');
    if (next == StringView::npos)
      break;
    StringView line = buf_view.substr(0, next);
    buf_view = buf_view.substr(next + 1);  // Eat the current line.
    while (!line.empty() && (line.at(line.size() - 1) == '\r' ||
                             line.at(line.size() - 1) == '\n')) {
      line = line.substr(0, line.size() - 1);
    }

    if (!has_parsed_first_line) {
      // Parse the "GET /xxx HTTP/1.1" line.
      has_parsed_first_line = true;
      size_t space = line.find(' ');
      if (space == std::string::npos || space + 2 >= line.size()) {
        conn->SendResponseAndClose("400 Bad Request");
        return 0;
      }
      http_req.method = line.substr(0, space);
      size_t uri_size = line.find(' ', space + 1) - (space + 1);
      http_req.uri = line.substr(space + 1, uri_size);
    } else if (line.empty()) {
      all_headers_received = true;
      // The CR-LF marker that separates headers from body.
      break;
    } else {
      // Parse HTTP headers, e.g. "Content-Length: 1234".
      size_t col = line.find(':');
      if (col == StringView::npos) {
        PERFETTO_DLOG("[HTTP] Malformed HTTP header: \"%s\"",
                      line.ToStdString().c_str());
        conn->SendResponseAndClose("400 Bad Request", {}, "Bad HTTP header");
        return 0;
      }
      auto hdr_name = line.substr(0, col);
      auto hdr_value = line.substr(col + 2);
      if (http_req.num_headers < http_req.headers.size()) {
        http_req.headers[http_req.num_headers++] = {hdr_name, hdr_value};
      } else {
        conn->SendResponseAndClose("400 Bad Request", {},
                                   "Too many HTTP headers");
      }

      if (hdr_name.CaseInsensitiveEq("content-length")) {
        body_size = static_cast<size_t>(atoi(hdr_value.ToStdString().c_str()));
      } else if (hdr_name.CaseInsensitiveEq("origin")) {
        http_req.origin = hdr_value;
        if (IsOriginAllowed(hdr_value))
          conn->origin_allowed_ = hdr_value.ToStdString();
      } else if (hdr_name.CaseInsensitiveEq("connection")) {
        auto values = SplitString(hdr_value.ToStdString(), ",");
        for (auto& value : values) {
          value = ToLower(TrimWhitespace(value));
        }
        conn->keepalive_ = Contains(values, "keep-alive");
        http_req.is_websocket_handshake = Contains(values, "upgrade");
      }
    }
  }

  // At this point |buf_view| has been stripped of the header and contains the
  // request body. We don't know yet if we have all the bytes for it or not.
  PERFETTO_CHECK(buf_view.size() <= conn->rxbuf_used);
  const size_t headers_size = conn->rxbuf_used - buf_view.size();

  if (body_size + headers_size >= kMaxRequestSize ||
      body_size > kMaxPayloadSize) {
    conn->SendResponseAndClose("413 Payload Too Large");
    return 0;
  }

  // If we can't read the full request return and try again next time with more
  // data.
  if (!all_headers_received || buf_view.size() < body_size)
    return 0;

  http_req.body = buf_view.substr(0, body_size);

  PERFETTO_LOG("[HTTP] %.*s %.*s [body=%zuB, origin=\"%.*s\"]",
               static_cast<int>(http_req.method.size()), http_req.method.data(),
               static_cast<int>(http_req.uri.size()), http_req.uri.data(),
               http_req.body.size(), static_cast<int>(http_req.origin.size()),
               http_req.origin.data());

  if (http_req.method == "OPTIONS") {
    HandleCorsPreflightRequest(http_req);
  } else {
    // Let the HttpHandler handle the request.
    req_handler_->OnHttpRequest(http_req);
  }

  // The handler is expected to send a response. If not, bail with a HTTP 500.
  if (!conn->headers_sent_)
    conn->SendResponseAndClose("500 Internal Server Error");

  // Allow chaining multiple responses in the same HTTP-Keepalive connection.
  conn->headers_sent_ = false;

  return headers_size + body_size;
}

void HttpServer::HandleCorsPreflightRequest(const HttpRequest& req) {
  req.conn->SendResponseAndClose(
      "204 No Content",
      {
          "Access-Control-Allow-Methods: POST, GET, OPTIONS",  //
          "Access-Control-Allow-Headers: *",                   //
          "Access-Control-Max-Age: 86400",                     //
          "Access-Control-Allow-Private-Network: true",        //
      });
}

bool HttpServer::IsOriginAllowed(StringView origin) {
  for (const std::string& allowed_origin : allowed_origins_) {
    if (origin.CaseInsensitiveEq(StringView(allowed_origin))) {
      return true;
    }
  }
  if (!origin_error_logged_ && !origin.empty()) {
    origin_error_logged_ = true;
    PERFETTO_ELOG(
        "[HTTP] The origin \"%.*s\" is not allowed, Access-Control-Allow-Origin"
        " won't be emitted. If this request comes from a browser it will fail.",
        static_cast<int>(origin.size()), origin.data());
  }
  return false;
}

void HttpServerConnection::UpgradeToWebsocket(const HttpRequest& req) {
  PERFETTO_CHECK(req.is_websocket_handshake);

  // |origin_allowed_| is set to the req.origin only if it's in the allowlist.
  if (origin_allowed_.empty())
    return SendResponseAndClose("403 Forbidden", {}, "Origin not allowed");

  auto ws_ver = req.GetHeader("sec-webSocket-version").value_or(StringView());
  auto ws_key = req.GetHeader("sec-webSocket-key").value_or(StringView());

  if (!ws_ver.CaseInsensitiveEq("13"))
    return SendResponseAndClose("505 HTTP Version Not Supported", {});

  if (ws_key.size() != 24) {
    // The nonce must be a base64 encoded 16 bytes value (24 after base64).
    return SendResponseAndClose("400 Bad Request", {});
  }

  // From https://datatracker.ietf.org/doc/html/rfc6455#section-1.3 :
  // For this header field, the server has to take the value (as present
  // in the header field, e.g., the base64-encoded [RFC4648] version minus
  // any leading and trailing whitespace) and concatenate this with the
  // Globally Unique Identifier (GUID, [RFC4122]) "258EAFA5-E914-47DA-
  // 95CA-C5AB0DC85B11" in string form, which is unlikely to be used by
  // network endpoints that do not understand the WebSocket Protocol.  A
  // SHA-1 hash (160 bits) [FIPS.180-3], base64-encoded (see Section 4 of
  // [RFC4648]), of this concatenation is then returned in the server's
  // handshake.
  StackString<128> signed_nonce("%.*s%s", static_cast<int>(ws_key.size()),
                                ws_key.data(), kWebsocketGuid);
  auto digest = SHA1Hash(signed_nonce.c_str(), signed_nonce.len());
  std::string digest_b64 = Base64Encode(digest.data(), digest.size());

  StackString<128> accept_hdr("Sec-WebSocket-Accept: %s", digest_b64.c_str());

  std::initializer_list<const char*> headers = {
      "Upgrade: websocket",   //
      "Connection: Upgrade",  //
      accept_hdr.c_str(),     //
  };
  PERFETTO_DLOG("[HTTP] Handshaking WebSocket for %.*s",
                static_cast<int>(req.uri.size()), req.uri.data());
  for (const char* hdr : headers)
    PERFETTO_DLOG("> %s", hdr);

  SendResponseHeaders("101 Switching Protocols", headers,
                      HttpServerConnection::kOmitContentLength);

  is_websocket_ = true;
}

size_t HttpServer::ParseOneWebsocketFrame(HttpServerConnection* conn) {
  auto* rxbuf = reinterpret_cast<uint8_t*>(conn->rxbuf.Get());
  const size_t frame_size = conn->rxbuf_used;
  uint8_t* rd = rxbuf;
  uint8_t* const end = rxbuf + frame_size;

  auto avail = [&] {
    PERFETTO_CHECK(rd <= end);
    return static_cast<size_t>(end - rd);
  };

  // From https://datatracker.ietf.org/doc/html/rfc6455#section-5.2 :
  //   0                   1                   2                   3
  //   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
  //  +-+-+-+-+-------+-+-------------+-------------------------------+
  //  |F|R|R|R| opcode|M| Payload len |    Extended payload length    |
  //  |I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
  //  |N|V|V|V|       |S|             |   (if payload len==126/127)   |
  //  | |1|2|3|       |K|             |                               |
  //  +-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
  //  |     Extended payload length continued, if payload len == 127  |
  //  + - - - - - - - - - - - - - - - +-------------------------------+
  //  |                               |Masking-key, if MASK set to 1  |
  //  +-------------------------------+-------------------------------+
  //  | Masking-key (continued)       |          Payload Data         |
  //  +-------------------------------- - - - - - - - - - - - - - - - +
  //  :                     Payload Data continued ...                :
  //  + - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - +
  //  |                     Payload Data continued ...                |
  //  +---------------------------------------------------------------+

  if (avail() < 2)
    return 0;  // Can't even decode the frame header. Wait for more data.

  uint8_t h0 = *(rd++);
  uint8_t h1 = *(rd++);
  const uint8_t opcode = h0 & 0x0F;

  const bool has_mask = !!(h1 & 0x80);
  uint64_t payload_len_u64 = (h1 & 0x7F);
  uint8_t extended_payload_size = 0;
  if (payload_len_u64 == 126) {
    extended_payload_size = 2;
  } else if (payload_len_u64 == 127) {
    extended_payload_size = 8;
  }

  if (extended_payload_size > 0) {
    if (avail() < extended_payload_size)
      return 0;  // Not enough data to read the extended header.
    payload_len_u64 = 0;
    for (uint8_t i = 0; i < extended_payload_size; ++i) {
      payload_len_u64 <<= 8;
      payload_len_u64 |= *(rd++);
    }
  }

  if (payload_len_u64 >= kMaxPayloadSize) {
    PERFETTO_ELOG("[HTTP] Websocket payload too big (%" PRIu64 " > %zu)",
                  payload_len_u64, kMaxPayloadSize);
    conn->Close();
    return 0;
  }
  const size_t payload_len = static_cast<size_t>(payload_len_u64);

  if (!has_mask) {
    // https://datatracker.ietf.org/doc/html/rfc6455#section-5.1
    // The server MUST close the connection upon receiving a frame that is
    // not masked.
    PERFETTO_ELOG("[HTTP] Websocket inbound frames must be masked");
    conn->Close();
    return 0;
  }

  uint8_t mask[4];
  if (avail() < sizeof(mask))
    return 0;  // Not enough data to read the masking key.
  memcpy(mask, rd, sizeof(mask));
  rd += sizeof(mask);

  if (avail() < payload_len)
    return 0;  // Not enough data to read the payload.
  uint8_t* const payload_start = rd;

  // Unmask the payload.
  for (uint32_t i = 0; i < payload_len; ++i)
    payload_start[i] ^= mask[i % sizeof(mask)];

  if (opcode == kOpcodePing) {
    PERFETTO_DLOG("[HTTP] Websocket PING");
    conn->SendWebsocketFrame(kOpcodePong, payload_start, payload_len);
  } else if (opcode == kOpcodeBinary || opcode == kOpcodeText ||
             opcode == kOpcodeContinuation) {
    // We do NOT handle fragmentation. We propagate all fragments as individual
    // messages, breaking the message-oriented nature of websockets. We do this
    // because in all our use cases we need only a byte stream without caring
    // about message boundaries.
    // If we wanted to support fragmentation, we'd have to stash
    // kOpcodeContinuation messages in a buffer, until we FIN bit is set.
    // When loading traces with trace processor, the messages can be up to
    // 32MB big (SLICE_SIZE in trace_stream.ts). The double-buffering would
    // slow down significantly trace loading with no benefits.
    WebsocketMessage msg(conn);
    msg.data =
        StringView(reinterpret_cast<const char*>(payload_start), payload_len);
    msg.is_text = opcode == kOpcodeText;
    req_handler_->OnWebsocketMessage(msg);
  } else if (opcode == kOpcodeClose) {
    conn->Close();
  } else {
    PERFETTO_LOG("Unsupported WebSocket opcode: %d", opcode);
  }
  return static_cast<size_t>(rd - rxbuf) + payload_len;
}

void HttpServerConnection::SendResponseHeaders(
    const char* http_code,
    std::initializer_list<const char*> headers,
    size_t content_length) {
  PERFETTO_CHECK(!headers_sent_);
  PERFETTO_CHECK(!is_websocket_);
  headers_sent_ = true;
  std::vector<char> resp_hdr;
  resp_hdr.reserve(512);
  bool has_connection_header = false;

  auto append = [&resp_hdr](const char* str) {
    resp_hdr.insert(resp_hdr.end(), str, str + strlen(str));
  };

  append("HTTP/1.1 ");
  append(http_code);
  append("\r\n");
  for (const char* hdr_cstr : headers) {
    StringView hdr = (hdr_cstr);
    if (hdr.empty())
      continue;
    has_connection_header |= hdr.substr(0, 11).CaseInsensitiveEq("connection:");
    append(hdr_cstr);
    append("\r\n");
  }
  content_len_actual_ = 0;
  content_len_headers_ = content_length;
  if (content_length != kOmitContentLength) {
    append("Content-Length: ");
    append(std::to_string(content_length).c_str());
    append("\r\n");
  }
  if (!has_connection_header) {
    // Various clients (e.g., python's http.client) assume that a HTTP
    // connection is keep-alive if the server says nothing, even when they do
    // NOT ask for it. Hence we must be explicit. If we are about to close the
    // connection, we must say so.
    append(keepalive_ ? "Connection: keep-alive\r\n" : "Connection: close\r\n");
  }
  if (!origin_allowed_.empty()) {
    append("Access-Control-Allow-Origin: ");
    append(origin_allowed_.c_str());
    append("\r\n");
    append("Vary: Origin\r\n");
  }
  append("\r\n");  // End-of-headers marker.
  sock->Send(resp_hdr.data(),
             resp_hdr.size());  // Send response headers.
}

void HttpServerConnection::SendResponseBody(const void* data, size_t len) {
  PERFETTO_CHECK(!is_websocket_);
  if (data == nullptr) {
    PERFETTO_DCHECK(len == 0);
    return;
  }
  content_len_actual_ += len;
  PERFETTO_CHECK(content_len_actual_ <= content_len_headers_ ||
                 content_len_headers_ == kOmitContentLength);
  sock->Send(data, len);
}

void HttpServerConnection::Close() {
  sock->Shutdown(/*notify=*/true);
}

void HttpServerConnection::SendResponse(
    const char* http_code,
    std::initializer_list<const char*> headers,
    StringView content,
    bool force_close) {
  if (force_close)
    keepalive_ = false;
  SendResponseHeaders(http_code, headers, content.size());
  SendResponseBody(content.data(), content.size());
  if (!keepalive_)
    Close();
}

void HttpServerConnection::SendWebsocketMessage(const void* data, size_t len) {
  SendWebsocketFrame(kOpcodeBinary, data, len);
}

void HttpServerConnection::SendWebsocketFrame(uint8_t opcode,
                                              const void* payload,
                                              size_t payload_len) {
  PERFETTO_CHECK(is_websocket_);

  uint8_t hdr[10]{};
  uint32_t hdr_len = 0;

  hdr[0] = opcode | 0x80 /* FIN=1, no fragmentation */;
  if (payload_len < 126) {
    hdr_len = 2;
    hdr[1] = static_cast<uint8_t>(payload_len);
  } else if (payload_len < 0xffff) {
    hdr_len = 4;
    hdr[1] = 126;  // Special value: Header extends for 2 bytes.
    uint16_t len_be = HostToBE16(static_cast<uint16_t>(payload_len));
    memcpy(&hdr[2], &len_be, sizeof(len_be));
  } else {
    hdr_len = 10;
    hdr[1] = 127;  // Special value: Header extends for 4 bytes.
    uint64_t len_be = HostToBE64(payload_len);
    memcpy(&hdr[2], &len_be, sizeof(len_be));
  }

  sock->Send(hdr, hdr_len);
  if (payload && payload_len > 0)
    sock->Send(payload, payload_len);
}

HttpServerConnection::HttpServerConnection(std::unique_ptr<UnixSocket> s)
    : sock(std::move(s)), rxbuf(PagedMemory::Allocate(kMaxRequestSize)) {}

HttpServerConnection::~HttpServerConnection() = default;

std::optional<StringView> HttpRequest::GetHeader(StringView name) const {
  for (size_t i = 0; i < num_headers; i++) {
    if (headers[i].name.CaseInsensitiveEq(name))
      return headers[i].value;
  }
  return std::nullopt;
}

HttpRequestHandler::~HttpRequestHandler() = default;
void HttpRequestHandler::OnWebsocketMessage(const WebsocketMessage&) {}
void HttpRequestHandler::OnHttpConnectionClosed(HttpServerConnection*) {}

}  // namespace perfetto::base
