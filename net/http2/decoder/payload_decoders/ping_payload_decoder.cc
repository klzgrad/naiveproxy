// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http2/decoder/payload_decoders/ping_payload_decoder.h"

#include "base/logging.h"
#include "net/http2/decoder/http2_frame_decoder_listener.h"
#include "net/http2/http2_constants.h"

namespace net {
namespace {
constexpr auto kOpaqueSize = Http2PingFields::EncodedSize();
}

DecodeStatus PingPayloadDecoder::StartDecodingPayload(FrameDecoderState* state,
                                                      DecodeBuffer* db) {
  const Http2FrameHeader& frame_header = state->frame_header();
  const uint32_t total_length = frame_header.payload_length;

  DVLOG(2) << "PingPayloadDecoder::StartDecodingPayload: " << frame_header;
  DCHECK_EQ(Http2FrameType::PING, frame_header.type);
  DCHECK_LE(db->Remaining(), total_length);
  DCHECK_EQ(0, frame_header.flags & ~(Http2FrameFlag::ACK));

  // Is the payload entirely in the decode buffer and is it the correct size?
  // Given the size of the header and payload (17 bytes total), this is most
  // likely the case the vast majority of the time.
  if (db->Remaining() == kOpaqueSize && total_length == kOpaqueSize) {
    // Special case this situation as it allows us to avoid any copying;
    // the other path makes two copies, first into the buffer in
    // Http2StructureDecoder as it accumulates the 8 bytes of opaque data,
    // and a second copy into the Http2PingFields member of in this class.
    // This supports the claim that this decoder is (mostly) non-buffering.
    static_assert(sizeof(Http2PingFields) == kOpaqueSize,
                  "If not, then can't enter this block!");
    auto* ping = reinterpret_cast<const Http2PingFields*>(db->cursor());
    if (frame_header.IsAck()) {
      state->listener()->OnPingAck(frame_header, *ping);
    } else {
      state->listener()->OnPing(frame_header, *ping);
    }
    db->AdvanceCursor(kOpaqueSize);
    return DecodeStatus::kDecodeDone;
  }
  state->InitializeRemainders();
  return HandleStatus(
      state, state->StartDecodingStructureInPayload(&ping_fields_, db));
}

DecodeStatus PingPayloadDecoder::ResumeDecodingPayload(FrameDecoderState* state,
                                                       DecodeBuffer* db) {
  DVLOG(2) << "ResumeDecodingPayload: remaining_payload="
           << state->remaining_payload();
  DCHECK_EQ(Http2FrameType::PING, state->frame_header().type);
  DCHECK_LE(db->Remaining(), state->frame_header().payload_length);
  return HandleStatus(
      state, state->ResumeDecodingStructureInPayload(&ping_fields_, db));
}

DecodeStatus PingPayloadDecoder::HandleStatus(FrameDecoderState* state,
                                              DecodeStatus status) {
  DVLOG(2) << "HandleStatus: status=" << status
           << "; remaining_payload=" << state->remaining_payload();
  if (status == DecodeStatus::kDecodeDone) {
    if (state->remaining_payload() == 0) {
      const Http2FrameHeader& frame_header = state->frame_header();
      if (frame_header.IsAck()) {
        state->listener()->OnPingAck(frame_header, ping_fields_);
      } else {
        state->listener()->OnPing(frame_header, ping_fields_);
      }
      return DecodeStatus::kDecodeDone;
    }
    // Payload is too long.
    return state->ReportFrameSizeError();
  }
  // Not done decoding the structure. Either we've got more payload to decode,
  // or we've run out because the payload is too short.
  DCHECK(
      (status == DecodeStatus::kDecodeInProgress &&
       state->remaining_payload() > 0) ||
      (status == DecodeStatus::kDecodeError && state->remaining_payload() == 0))
      << "\n status=" << status
      << "; remaining_payload=" << state->remaining_payload();
  return status;
}

}  // namespace net
