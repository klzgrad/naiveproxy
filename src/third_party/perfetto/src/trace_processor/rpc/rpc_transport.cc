/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "src/trace_processor/rpc/rpc_transport.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "perfetto/base/compiler.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/base64.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/unix_socket.h"
#include "src/trace_processor/rpc/session_paths.h"

namespace perfetto::trace_processor {
namespace {

// ---- AF_UNIX byte-pipe transport ------------------------------------------

class UnixByteTransport : public RpcTransport {
 public:
  explicit UnixByteTransport(base::UnixSocketRaw sock)
      : sock_(std::move(sock)) {}

  base::Status Send(const uint8_t* data, size_t len) override {
    size_t off = 0;
    while (off < len) {
      ssize_t n = sock_.Send(data + off, len - off);
      if (n <= 0)
        return base::ErrStatus("Failed to send request to session");
      off += static_cast<size_t>(n);
    }
    return base::OkStatus();
  }

  base::StatusOr<size_t> Recv(uint8_t* buf, size_t len) override {
    ssize_t n = sock_.Receive(buf, len);
    if (n < 0)
      return base::ErrStatus("Error reading from session");
    return static_cast<size_t>(n);
  }

 private:
  base::UnixSocketRaw sock_;
};

// ---- WebSocket transport (to the http server's /websocket) ----------------

class WebSocketTransport : public RpcTransport {
 public:
  explicit WebSocketTransport(base::UnixSocketRaw sock)
      : sock_(std::move(sock)) {}

  base::Status Handshake(const std::string& host_port);

  base::Status Send(const uint8_t* data, size_t len) override;
  base::StatusOr<size_t> Recv(uint8_t* buf, size_t len) override;

 private:
  // The decoded fixed-size part of a WebSocket frame header: everything up to,
  // but not including, the payload bytes. See RFC 6455 §5.2.
  struct FrameHeader {
    uint8_t opcode = 0;              // 0x1 text, 0x2 binary, 0x8 close, etc.
    bool masked = false;             // Whether the payload is XOR-masked.
    uint64_t payload_len = 0;        // Number of payload bytes that follow.
    uint8_t mask[4] = {0, 0, 0, 0};  // Mask key, valid only if |masked|.
  };

  // Blocking read of exactly |n| bytes. Returns false on EOF/error.
  bool ReadExactly(void* dst, size_t n);
  // Reads and decodes the next frame header off the wire (base bytes, extended
  // length and, if present, the mask key). Returns false on EOF/error.
  bool ReadFrameHeader(FrameHeader* out);
  // Reads the |hdr.payload_len| payload bytes that follow |hdr|, unmasking them
  // in place if needed. Returns false on EOF/error.
  bool ReadFramePayload(const FrameHeader& hdr, std::string* payload);
  // Reads complete WebSocket frames until a data frame arrives, appending its
  // payload to inbound_. Control frames (ping/pong) are skipped. Returns false
  // if the connection closed.
  bool ReadFrameIntoBuffer();

  base::UnixSocketRaw sock_;
  std::string inbound_;  // Un-consumed payload bytes from received frames.
  size_t inbound_pos_ = 0;
};

bool WebSocketTransport::ReadExactly(void* dst, size_t n) {
  auto* p = static_cast<uint8_t*>(dst);
  size_t got = 0;
  while (got < n) {
    ssize_t r = sock_.Receive(p + got, n - got);
    if (r <= 0)
      return false;
    got += static_cast<size_t>(r);
  }
  return true;
}

base::Status WebSocketTransport::Handshake(const std::string& host_port) {
  // The server requires a Sec-WebSocket-Key of 16 base64-decoded bytes and an
  // Origin in its allow-list (defaults include http://localhost:10000). It does
  // not validate the key's value, so a fixed key is fine.
  std::string key = base::Base64Encode("perfetto-tp-wsclient", 16);
  std::string req =
      "GET /websocket HTTP/1.1\r\n"
      "Host: " +
      host_port +
      "\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Version: 13\r\n"
      "Origin: http://localhost:10000\r\n"
      "Sec-WebSocket-Key: " +
      key + "\r\n\r\n";
  if (sock_.Send(req.data(), req.size()) < 0)
    return base::ErrStatus("WebSocket handshake send failed");

  // Read response headers up to the blank line. Keep it simple: read byte by
  // byte until "\r\n\r\n". Handshake responses are small.
  std::string resp;
  char c;
  while (resp.find("\r\n\r\n") == std::string::npos) {
    ssize_t r = sock_.Receive(&c, 1);
    if (r <= 0)
      return base::ErrStatus("WebSocket handshake closed by peer");
    resp.push_back(c);
    if (resp.size() > 8192)
      return base::ErrStatus("WebSocket handshake response too large");
  }
  if (resp.find("101") == std::string::npos) {
    size_t eol = resp.find("\r\n");
    return base::ErrStatus("WebSocket upgrade rejected: %s",
                           resp.substr(0, eol).c_str());
  }
  return base::OkStatus();
}

base::Status WebSocketTransport::Send(const uint8_t* data, size_t len) {
  // Client frames must be masked (RFC 6455). A zero mask leaves the payload
  // unchanged, which the server unmasks back to the original bytes.
  std::string frame;
  frame.push_back(static_cast<char>(0x82));  // FIN | binary opcode.
  if (len < 126) {
    frame.push_back(static_cast<char>(0x80 | len));
  } else if (len <= 0xFFFF) {
    frame.push_back(static_cast<char>(0x80 | 126));
    frame.push_back(static_cast<char>((len >> 8) & 0xFF));
    frame.push_back(static_cast<char>(len & 0xFF));
  } else {
    frame.push_back(static_cast<char>(0x80 | 127));
    for (int shift = 56; shift >= 0; shift -= 8)
      frame.push_back(static_cast<char>((len >> shift) & 0xFF));
  }
  frame.append(4, '\0');  // 4-byte mask key, all zeros.
  frame.append(reinterpret_cast<const char*>(data), len);

  size_t off = 0;
  while (off < frame.size()) {
    ssize_t n = sock_.Send(frame.data() + off, frame.size() - off);
    if (n <= 0)
      return base::ErrStatus("Failed to send to WebSocket session");
    off += static_cast<size_t>(n);
  }
  return base::OkStatus();
}

PERFETTO_ALWAYS_INLINE bool WebSocketTransport::ReadFrameHeader(
    FrameHeader* out) {
  // Byte 0 holds FIN/RSV/opcode; byte 1 holds the MASK bit and the 7-bit base
  // payload length. We ignore FIN and assume unfragmented frames (the server
  // never fragments our RPC responses).
  uint8_t base[2];
  if (!ReadExactly(base, 2))
    return false;
  out->opcode = base[0] & 0x0F;
  out->masked = (base[1] & 0x80) != 0;

  // The 7-bit length is either the real length, or a sentinel saying the real
  // length follows in a 16-bit (126) or 64-bit (127) big-endian field.
  uint64_t len = base[1] & 0x7F;
  if (len == 126) {
    uint8_t ext[2];
    if (!ReadExactly(ext, 2))
      return false;
    len = (static_cast<uint64_t>(ext[0]) << 8) | ext[1];
  } else if (len == 127) {
    uint8_t ext[8];
    if (!ReadExactly(ext, 8))
      return false;
    len = 0;
    for (uint8_t b : ext)
      len = (len << 8) | b;
  }
  out->payload_len = len;

  // A masked frame is followed by a 4-byte mask key, used to XOR the payload.
  if (out->masked && !ReadExactly(out->mask, 4))
    return false;
  return true;
}

PERFETTO_ALWAYS_INLINE bool WebSocketTransport::ReadFramePayload(
    const FrameHeader& hdr,
    std::string* payload) {
  payload->resize(static_cast<size_t>(hdr.payload_len));
  if (hdr.payload_len > 0 && !ReadExactly(payload->data(), payload->size()))
    return false;
  // Each payload byte is XORed with mask[i % 4] to recover the original bytes.
  if (hdr.masked) {
    for (size_t i = 0; i < payload->size(); ++i)
      (*payload)[i] = static_cast<char>((*payload)[i] ^ hdr.mask[i % 4]);
  }
  return true;
}

bool WebSocketTransport::ReadFrameIntoBuffer() {
  // The server can interleave control frames (ping/pong) at any point, so loop
  // until we get a data frame, skipping anything that isn't payload for us.
  for (;;) {
    FrameHeader hdr;
    if (!ReadFrameHeader(&hdr))
      return false;

    switch (hdr.opcode) {
      case 0x1:  // Text.
      case 0x2:  // Binary.
        // Read straight into inbound_, which Recv has already emptied before
        // calling us, so the payload is never copied a second time.
        if (!ReadFramePayload(hdr, &inbound_))
          return false;
        inbound_pos_ = 0;
        return true;
      case 0x8:  // Close.
        return false;
      case 0x9:  // Ping: ignore (server tolerates a missing pong for our use).
      case 0xA:  // Pong.
      default: {
        // Drain and discard the control frame's payload, then read the next.
        std::string discard;
        if (!ReadFramePayload(hdr, &discard))
          return false;
        continue;
      }
    }
  }
}

base::StatusOr<size_t> WebSocketTransport::Recv(uint8_t* buf, size_t len) {
  if (inbound_pos_ >= inbound_.size()) {
    inbound_.clear();
    inbound_pos_ = 0;
    if (!ReadFrameIntoBuffer())
      return size_t{0};  // Closed.
  }
  size_t avail = inbound_.size() - inbound_pos_;
  size_t n = avail < len ? avail : len;
  memcpy(buf, inbound_.data() + inbound_pos_, n);
  inbound_pos_ += n;
  return n;
}

}  // namespace

RpcTransport::~RpcTransport() = default;

base::StatusOr<std::unique_ptr<RpcTransport>> ConnectRpcTransport(
    const std::string& addr) {
  switch (session::ClassifyRemoteAddr(addr)) {
    case session::RemoteAddrKind::kHttp: {
      auto sock = base::UnixSocketRaw::CreateMayFail(base::SockFamily::kInet,
                                                     base::SockType::kStream);
      sock.SetBlocking(true);
      if (!sock || !sock.Connect(addr)) {
        return base::ErrStatus("Could not connect to '%s'", addr.c_str());
      }
      auto ws = std::make_unique<WebSocketTransport>(std::move(sock));
      RETURN_IF_ERROR(ws->Handshake(addr));
      return std::unique_ptr<RpcTransport>(std::move(ws));
    }
    case session::RemoteAddrKind::kUnixPath:
    case session::RemoteAddrKind::kSessionName: {
      std::string socket_path = addr;
      if (session::ClassifyRemoteAddr(addr) ==
          session::RemoteAddrKind::kSessionName) {
        ASSIGN_OR_RETURN(socket_path, session::SessionSocketPath(addr));
      }
      auto sock = base::UnixSocketRaw::CreateMayFail(base::SockFamily::kUnix,
                                                     base::SockType::kStream);
      if (!sock || !sock.Connect(socket_path)) {
        return base::ErrStatus(
            "No live session at '%s'. Start one with: tp server unix --name "
            "<name> <trace>",
            addr.c_str());
      }
      sock.SetBlocking(true);
      return std::unique_ptr<RpcTransport>(
          std::make_unique<UnixByteTransport>(std::move(sock)));
    }
  }
  PERFETTO_FATAL("Unreachable");
}

}  // namespace perfetto::trace_processor
