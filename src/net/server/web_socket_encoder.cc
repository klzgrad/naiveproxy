// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef UNSAFE_BUFFERS_BUILD
// TODO(crbug.com/40284755): Remove this and spanify to fix the errors.
#pragma allow_unsafe_buffers
#endif

#include "net/server/web_socket_encoder.h"

#include <limits>
#include <string_view>
#include <utility>

#include "base/check.h"
#include "base/memory/ptr_util.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "net/base/io_buffer.h"
#include "net/base/net_export.h"
#include "net/websockets/websocket_deflate_parameters.h"
#include "net/websockets/websocket_extension.h"
#include "net/websockets/websocket_extension_parser.h"
#include "net/websockets/websocket_frame.h"

namespace net {

NET_EXPORT
const char WebSocketEncoder::kClientExtensions[] =
    "permessage-deflate; client_max_window_bits";

namespace {

const int kInflaterChunkSize = 16 * 1024;

// Constants for hybi-10 frame format.

const unsigned char kFinalBit = 0x80;
const unsigned char kReserved1Bit = 0x40;
const unsigned char kReserved2Bit = 0x20;
const unsigned char kReserved3Bit = 0x10;
const unsigned char kOpCodeMask = 0xF;
const unsigned char kMaskBit = 0x80;
const unsigned char kPayloadLengthMask = 0x7F;

const size_t kMaxSingleBytePayloadLength = 125;
const size_t kTwoBytePayloadLengthField = 126;
const size_t kEightBytePayloadLengthField = 127;
const size_t kMaskingKeyWidthInBytes = 4;

WebSocketParseResult DecodeFrameHybi17(std::string_view frame,
                                       bool client_frame,
                                       int* bytes_consumed,
                                       std::string* output,
                                       bool* compressed) {
  size_t data_length = frame.length();
  if (data_length < 2)
    return WebSocketParseResult::FRAME_INCOMPLETE;

  const char* buffer_begin = const_cast<char*>(frame.data());
  const char* p = buffer_begin;
  const char* buffer_end = p + data_length;

  unsigned char first_byte = *p++;
  unsigned char second_byte = *p++;

  bool final = (first_byte & kFinalBit) != 0;
  bool reserved1 = (first_byte & kReserved1Bit) != 0;
  bool reserved2 = (first_byte & kReserved2Bit) != 0;
  bool reserved3 = (first_byte & kReserved3Bit) != 0;
  int op_code = first_byte & kOpCodeMask;
  bool masked = (second_byte & kMaskBit) != 0;
  *compressed = reserved1;
  if (reserved2 || reserved3)
    return WebSocketParseResult::FRAME_ERROR;  // Only compression extension is
                                               // supported.

  bool closed = false;
  switch (op_code) {
    case WebSocketFrameHeader::OpCodeEnum::kOpCodeClose:
      closed = true;
      break;

    case WebSocketFrameHeader::OpCodeEnum::kOpCodeText:
    case WebSocketFrameHeader::OpCodeEnum::
        kOpCodeContinuation:  // Treated in the same as kOpCodeText.
    case WebSocketFrameHeader::OpCodeEnum::kOpCodePing:
    case WebSocketFrameHeader::OpCodeEnum::kOpCodePong:
      break;

    case WebSocketFrameHeader::OpCodeEnum::kOpCodeBinary:  // We don't support
                                                           // binary frames yet.
    default:
      return WebSocketParseResult::FRAME_ERROR;
  }

  if (client_frame && !masked)  // In Hybi-17 spec client MUST mask its frame.
    return WebSocketParseResult::FRAME_ERROR;

  uint64_t payload_length64 = second_byte & kPayloadLengthMask;
  if (payload_length64 > kMaxSingleBytePayloadLength) {
    int extended_payload_length_size;
    if (payload_length64 == kTwoBytePayloadLengthField) {
      extended_payload_length_size = 2;
    } else {
      DCHECK(payload_length64 == kEightBytePayloadLengthField);
      extended_payload_length_size = 8;
    }
    if (buffer_end - p < extended_payload_length_size)
      return WebSocketParseResult::FRAME_INCOMPLETE;
    payload_length64 = 0;
    for (int i = 0; i < extended_payload_length_size; ++i) {
      payload_length64 <<= 8;
      payload_length64 |= static_cast<unsigned char>(*p++);
    }
  }

  size_t actual_masking_key_length = masked ? kMaskingKeyWidthInBytes : 0;
  static const uint64_t max_payload_length = 0x7FFFFFFFFFFFFFFFull;
  static size_t max_length = std::numeric_limits<size_t>::max();
  if (payload_length64 > max_payload_length ||
      payload_length64 + actual_masking_key_length > max_length) {
    // WebSocket frame length too large.
    return WebSocketParseResult::FRAME_ERROR;
  }
  size_t payload_length = static_cast<size_t>(payload_length64);

  size_t total_length = actual_masking_key_length + payload_length;
  if (static_cast<size_t>(buffer_end - p) < total_length)
    return WebSocketParseResult::FRAME_INCOMPLETE;

  if (masked) {
    output->resize(payload_length);
    const char* masking_key = p;
    char* payload = const_cast<char*>(p + kMaskingKeyWidthInBytes);
    for (size_t i = 0; i < payload_length; ++i)  // Unmask the payload.
      (*output)[i] = payload[i] ^ masking_key[i % kMaskingKeyWidthInBytes];
  } else {
    output->assign(p, p + payload_length);
  }

  size_t pos = p + actual_masking_key_length + payload_length - buffer_begin;
  *bytes_consumed = pos;

  if (op_code == WebSocketFrameHeader::OpCodeEnum::kOpCodePing)
    return WebSocketParseResult::FRAME_PING;

  if (op_code == WebSocketFrameHeader::OpCodeEnum::kOpCodePong)
    return WebSocketParseResult::FRAME_PONG;

  if (closed)
    return WebSocketParseResult::FRAME_CLOSE;

  return final ? WebSocketParseResult::FRAME_OK_FINAL
               : WebSocketParseResult::FRAME_OK_MIDDLE;
}

void EncodeFrameHybi17(std::string_view message,
                       int masking_key,
                       bool compressed,
                       WebSocketFrameHeader::OpCodeEnum op_code,
                       std::string* output) {
  std::vector<char> frame;
  size_t data_length = message.length();

  int reserved1 = compressed ? kReserved1Bit : 0;
  frame.push_back(kFinalBit | op_code | reserved1);
  char mask_key_bit = masking_key != 0 ? kMaskBit : 0;
  if (data_length <= kMaxSingleBytePayloadLength) {
    frame.push_back(static_cast<char>(data_length) | mask_key_bit);
  } else if (data_length <= 0xFFFF) {
    frame.push_back(kTwoBytePayloadLengthField | mask_key_bit);
    frame.push_back((data_length & 0xFF00) >> 8);
    frame.push_back(data_length & 0xFF);
  } else {
    frame.push_back(kEightBytePayloadLengthField | mask_key_bit);
    char extended_payload_length[8];
    size_t remaining = data_length;
    // Fill the length into extended_payload_length in the network byte order.
    for (int i = 0; i < 8; ++i) {
      extended_payload_length[7 - i] = remaining & 0xFF;
      remaining >>= 8;
    }
    frame.insert(frame.end(), extended_payload_length,
                 extended_payload_length + 8);
    DCHECK(!remaining);
  }

  const char* data = const_cast<char*>(message.data());
  if (masking_key != 0) {
    const char* mask_bytes = reinterpret_cast<char*>(&masking_key);
    frame.insert(frame.end(), mask_bytes, mask_bytes + 4);
    for (size_t i = 0; i < data_length; ++i)  // Mask the payload.
      frame.push_back(data[i] ^ mask_bytes[i % kMaskingKeyWidthInBytes]);
  } else {
    frame.insert(frame.end(), data, data + data_length);
  }
  *output = std::string(frame.data(), frame.size());
}

}  // anonymous namespace

// static
std::unique_ptr<WebSocketEncoder> WebSocketEncoder::CreateServer() {
  return base::WrapUnique(new WebSocketEncoder(FOR_SERVER, nullptr, nullptr));
}

// static
std::unique_ptr<WebSocketEncoder> WebSocketEncoder::CreateServer(
    const std::string& extensions,
    WebSocketDeflateParameters* deflate_parameters) {
  WebSocketExtensionParser parser;
  if (!parser.Parse(extensions)) {
    // Failed to parse Sec-WebSocket-Extensions header. We MUST fail the
    // connection.
    return nullptr;
  }

  for (const auto& extension : parser.extensions()) {
    std::string failure_message;
    WebSocketDeflateParameters offer;
    if (!offer.Initialize(extension, &failure_message) ||
        !offer.IsValidAsRequest(&failure_message)) {
      // We decline unknown / malformed extensions.
      continue;
    }

    WebSocketDeflateParameters response = offer;
    if (offer.is_client_max_window_bits_specified() &&
        !offer.has_client_max_window_bits_value()) {
      // We need to choose one value for the response.
      response.SetClientMaxWindowBits(15);
    }
    DCHECK(response.IsValidAsResponse());
    DCHECK(offer.IsCompatibleWith(response));
    auto deflater = std::make_unique<WebSocketDeflater>(
        response.server_context_take_over_mode());
    auto inflater = std::make_unique<WebSocketInflater>(kInflaterChunkSize,
                                                        kInflaterChunkSize);
    if (!deflater->Initialize(response.PermissiveServerMaxWindowBits()) ||
        !inflater->Initialize(response.PermissiveClientMaxWindowBits())) {
      // For some reason we cannot accept the parameters.
      continue;
    }
    *deflate_parameters = response;
    return base::WrapUnique(new WebSocketEncoder(
        FOR_SERVER, std::move(deflater), std::move(inflater)));
  }

  // We cannot find an acceptable offer.
  return base::WrapUnique(new WebSocketEncoder(FOR_SERVER, nullptr, nullptr));
}

// static
std::unique_ptr<WebSocketEncoder> WebSocketEncoder::CreateClient(
    const std::string& response_extensions) {
  // TODO(yhirano): Add a way to return an error.

  WebSocketExtensionParser parser;
  if (!parser.Parse(response_extensions)) {
    // Parse error. Note that there are two cases here.
    // 1) There is no Sec-WebSocket-Extensions header.
    // 2) There is a malformed Sec-WebSocketExtensions header.
    // We should return a deflate-disabled encoder for the former case and
    // fail the connection for the latter case.
    return base::WrapUnique(new WebSocketEncoder(FOR_CLIENT, nullptr, nullptr));
  }
  if (parser.extensions().size() != 1) {
    // Only permessage-deflate extension is supported.
    // TODO (yhirano): Fail the connection.
    return base::WrapUnique(new WebSocketEncoder(FOR_CLIENT, nullptr, nullptr));
  }
  const auto& extension = parser.extensions()[0];
  WebSocketDeflateParameters params;
  std::string failure_message;
  if (!params.Initialize(extension, &failure_message) ||
      !params.IsValidAsResponse(&failure_message)) {
    // TODO (yhirano): Fail the connection.
    return base::WrapUnique(new WebSocketEncoder(FOR_CLIENT, nullptr, nullptr));
  }

  auto deflater = std::make_unique<WebSocketDeflater>(
      params.client_context_take_over_mode());
  auto inflater = std::make_unique<WebSocketInflater>(kInflaterChunkSize,
                                                      kInflaterChunkSize);
  if (!deflater->Initialize(params.PermissiveClientMaxWindowBits()) ||
      !inflater->Initialize(params.PermissiveServerMaxWindowBits())) {
    // TODO (yhirano): Fail the connection.
    return base::WrapUnique(new WebSocketEncoder(FOR_CLIENT, nullptr, nullptr));
  }

  return base::WrapUnique(new WebSocketEncoder(FOR_CLIENT, std::move(deflater),
                                               std::move(inflater)));
}

WebSocketEncoder::WebSocketEncoder(Type type,
                                   std::unique_ptr<WebSocketDeflater> deflater,
                                   std::unique_ptr<WebSocketInflater> inflater)
    : type_(type),
      deflater_(std::move(deflater)),
      inflater_(std::move(inflater)) {}

WebSocketEncoder::~WebSocketEncoder() = default;

WebSocketParseResult WebSocketEncoder::DecodeFrame(std::string_view frame,
                                                   int* bytes_consumed,
                                                   std::string* output) {
  bool compressed;
  std::string current_output;
  WebSocketParseResult result = DecodeFrameHybi17(
      frame, type_ == FOR_SERVER, bytes_consumed, &current_output, &compressed);
  switch (result) {
    case WebSocketParseResult::FRAME_OK_FINAL:
    case WebSocketParseResult::FRAME_OK_MIDDLE: {
      if (continuation_message_frames_.empty())
        is_current_message_compressed_ = compressed;
      continuation_message_frames_.push_back(current_output);

      if (result == WebSocketParseResult::FRAME_OK_FINAL) {
        *output = base::StrCat(continuation_message_frames_);
        continuation_message_frames_.clear();
        if (is_current_message_compressed_ && !Inflate(output)) {
          return WebSocketParseResult::FRAME_ERROR;
        }
      }
      break;
    }

    case WebSocketParseResult::FRAME_PING:
      *output = current_output;
      break;

    default:
      // This function doesn't need special handling for other parse results.
      break;
  }

  return result;
}

void WebSocketEncoder::EncodeTextFrame(std::string_view frame,
                                       int masking_key,
                                       std::string* output) {
  std::string compressed;
  constexpr auto op_code = WebSocketFrameHeader::OpCodeEnum::kOpCodeText;
  if (Deflate(frame, &compressed))
    EncodeFrameHybi17(compressed, masking_key, true, op_code, output);
  else
    EncodeFrameHybi17(frame, masking_key, false, op_code, output);
}

void WebSocketEncoder::EncodeCloseFrame(std::string_view frame,
                                        int masking_key,
                                        std::string* output) {
  constexpr auto op_code = WebSocketFrameHeader::OpCodeEnum::kOpCodeClose;
  EncodeFrameHybi17(frame, masking_key, false, op_code, output);
}

void WebSocketEncoder::EncodePongFrame(std::string_view frame,
                                       int masking_key,
                                       std::string* output) {
  constexpr auto op_code = WebSocketFrameHeader::OpCodeEnum::kOpCodePong;
  EncodeFrameHybi17(frame, masking_key, false, op_code, output);
}

bool WebSocketEncoder::Inflate(std::string* message) {
  if (!inflater_)
    return false;
  if (!inflater_->AddBytes(message->data(), message->length()))
    return false;
  if (!inflater_->Finish())
    return false;

  std::vector<char> output;
  while (inflater_->CurrentOutputSize() > 0) {
    scoped_refptr<IOBufferWithSize> chunk =
        inflater_->GetOutput(inflater_->CurrentOutputSize());
    if (!chunk.get())
      return false;
    output.insert(output.end(), chunk->data(), chunk->data() + chunk->size());
  }

  *message =
      output.size() ? std::string(output.data(), output.size()) : std::string();
  return true;
}

bool WebSocketEncoder::Deflate(std::string_view message, std::string* output) {
  if (!deflater_)
    return false;
  if (!deflater_->AddBytes(message.data(), message.length())) {
    deflater_->Finish();
    return false;
  }
  if (!deflater_->Finish())
    return false;
  scoped_refptr<IOBufferWithSize> buffer =
      deflater_->GetOutput(deflater_->CurrentOutputSize());
  if (!buffer.get())
    return false;
  *output = std::string(buffer->data(), buffer->size());
  return true;
}

}  // namespace net
