// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_WEBSOCKETS_WEBSOCKET_FRAME_H_
#define NET_WEBSOCKETS_WEBSOCKET_FRAME_H_

#include <stdint.h>

#include <memory>
#include <vector>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "net/base/net_export.h"

namespace net {

class IOBuffer;
class IOBufferWithSize;

// Represents a WebSocket frame header.
//
// Members of this class correspond to each element in WebSocket frame header
// (see http://tools.ietf.org/html/rfc6455#section-5.2).
struct NET_EXPORT WebSocketFrameHeader {
  typedef int OpCode;

  // Originally these constants were static const int, but to make it possible
  // to use them in a switch statement they were changed to an enum.
  enum OpCodeEnum {
    kOpCodeContinuation = 0x0,
    kOpCodeText = 0x1,
    kOpCodeBinary = 0x2,
    kOpCodeDataUnused = 0x3,
    kOpCodeClose = 0x8,
    kOpCodePing = 0x9,
    kOpCodePong = 0xA,
    kOpCodeControlUnused = 0xB,
  };

  // Return true if |opcode| is one of the data opcodes known to this
  // implementation.
  static bool IsKnownDataOpCode(OpCode opcode) {
    return opcode == kOpCodeContinuation || opcode == kOpCodeText ||
           opcode == kOpCodeBinary;
  }

  // Return true if |opcode| is one of the control opcodes known to this
  // implementation.
  static bool IsKnownControlOpCode(OpCode opcode) {
    return opcode == kOpCodeClose || opcode == kOpCodePing ||
           opcode == kOpCodePong;
  }

  // These values must be a compile-time constant. "enum hack" is used here
  // to make MSVC happy.
  enum {
    kBaseHeaderSize = 2,
    kMaximumExtendedLengthSize = 8,
    kMaskingKeyLength = 4
  };

  // Constructor to avoid a lot of repetitive initialisation.
  explicit WebSocketFrameHeader(OpCode opCode)
      : final(false),
        reserved1(false),
        reserved2(false),
        reserved3(false),
        opcode(opCode),
        masked(false),
        payload_length(0) {}

  // Create a clone of this object on the heap.
  std::unique_ptr<WebSocketFrameHeader> Clone() const;

  // Overwrite this object with the fields from |source|.
  void CopyFrom(const WebSocketFrameHeader& source);

  // Members below correspond to each item in WebSocket frame header.
  // See <http://tools.ietf.org/html/rfc6455#section-5.2> for details.
  bool final;
  bool reserved1;
  bool reserved2;
  bool reserved3;
  OpCode opcode;
  bool masked;
  uint64_t payload_length;

 private:
  DISALLOW_COPY_AND_ASSIGN(WebSocketFrameHeader);
};

// Contains an entire WebSocket frame including payload. This is used by APIs
// that are not concerned about retaining the original frame boundaries (because
// frames may need to be split in order for the data to fit in memory).
struct NET_EXPORT_PRIVATE WebSocketFrame {
  // A frame must always have an opcode, so this parameter is compulsory.
  explicit WebSocketFrame(WebSocketFrameHeader::OpCode opcode);
  ~WebSocketFrame();

  // |header| is always present.
  WebSocketFrameHeader header;

  // |data| is always unmasked even if the frame is masked. The size of |data|
  // is given by |header.payload_length|.
  scoped_refptr<IOBuffer> data;
};

// Structure describing one chunk of a WebSocket frame.
//
// The payload of a WebSocket frame may be divided into multiple chunks.
// You need to look at |final_chunk| member variable to detect the end of a
// series of chunk objects of a WebSocket frame.
//
// Frame dissection is necessary to handle frames that are too large to store in
// the browser memory without losing information about the frame boundaries. In
// practice, most code does not need to worry about the original frame
// boundaries and can use the WebSocketFrame type declared above.
//
// Users of this struct should treat WebSocket frames as a data stream; it's
// important to keep the frame data flowing, especially in the browser process.
// Users should not let the data stuck somewhere in the pipeline.
//
// This struct is used for reading WebSocket frame data (created by
// WebSocketFrameParser). To construct WebSocket frames, use functions below.
struct NET_EXPORT WebSocketFrameChunk {
  WebSocketFrameChunk();
  ~WebSocketFrameChunk();

  // Non-null |header| is provided only if this chunk is the first part of
  // a series of chunks.
  std::unique_ptr<WebSocketFrameHeader> header;

  // Indicates this part is the last chunk of a frame.
  bool final_chunk;

  // |data| is always unmasked even if the frame is masked. |data| might be
  // null in the first chunk.
  scoped_refptr<IOBufferWithSize> data;
};

// Contains four-byte data representing "masking key" of WebSocket frames.
struct WebSocketMaskingKey {
  char key[WebSocketFrameHeader::kMaskingKeyLength];
};

// Returns the size of WebSocket frame header. The size of WebSocket frame
// header varies from 2 bytes to 14 bytes depending on the payload length
// and maskedness.
NET_EXPORT int GetWebSocketFrameHeaderSize(const WebSocketFrameHeader& header);

// Writes wire format of a WebSocket frame header into |output|, and returns
// the number of bytes written.
//
// WebSocket frame format is defined at:
// <http://tools.ietf.org/html/rfc6455#section-5.2>. This function writes
// everything but payload data in a WebSocket frame to |buffer|.
//
// If |header->masked| is true, |masking_key| must point to a valid
// WebSocketMaskingKey object containing the masking key for that frame
// (possibly generated by GenerateWebSocketMaskingKey() function below).
// Otherwise, |masking_key| must be NULL.
//
// |buffer| should have enough size to contain the frame header.
// GetWebSocketFrameHeaderSize() can be used to know the size of header
// beforehand. If the size of |buffer| is insufficient, this function returns
// ERR_INVALID_ARGUMENT and does not write any data to |buffer|.
NET_EXPORT int WriteWebSocketFrameHeader(const WebSocketFrameHeader& header,
                                         const WebSocketMaskingKey* masking_key,
                                         char* buffer,
                                         int buffer_size);

// Generates a masking key suitable for use in a new WebSocket frame.
NET_EXPORT WebSocketMaskingKey GenerateWebSocketMaskingKey();

// Masks WebSocket frame payload.
//
// A client must mask every WebSocket frame by XOR'ing the frame payload
// with four-byte random data (masking key). This function applies the masking
// to the given payload data.
//
// This function masks |data| with |masking_key|, assuming |data| is partial
// data starting from |frame_offset| bytes from the beginning of the payload
// data.
//
// Since frame masking is a reversible operation, this function can also be
// used for unmasking a WebSocket frame.
NET_EXPORT void MaskWebSocketFramePayload(
    const WebSocketMaskingKey& masking_key,
    uint64_t frame_offset,
    char* data,
    int data_size);

}  // namespace net

#endif  // NET_WEBSOCKETS_WEBSOCKET_FRAME_H_
