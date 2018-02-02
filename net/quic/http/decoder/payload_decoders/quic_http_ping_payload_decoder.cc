// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/http/decoder/payload_decoders/quic_http_ping_payload_decoder.h"

#include <cstdint>

#include "base/logging.h"
#include "net/quic/http/decoder/quic_http_frame_decoder_listener.h"
#include "net/quic/http/quic_http_constants.h"

namespace net {
namespace {
constexpr auto kOpaqueSize = QuicHttpPingFields::EncodedSize();
}

QuicHttpDecodeStatus QuicHttpPingQuicHttpPayloadDecoder::StartDecodingPayload(
    QuicHttpFrameDecoderState* state,
    QuicHttpDecodeBuffer* db) {
  const QuicHttpFrameHeader& frame_header = state->frame_header();
  const uint32_t total_length = frame_header.payload_length;

  DVLOG(2) << "QuicHttpPingQuicHttpPayloadDecoder::StartDecodingPayload: "
           << frame_header;
  DCHECK_EQ(QuicHttpFrameType::PING, frame_header.type);
  DCHECK_LE(db->Remaining(), total_length);
  DCHECK_EQ(0, frame_header.flags & ~(QuicHttpFrameFlag::QUIC_HTTP_ACK));

  // Is the payload entirely in the decode buffer and is it the correct size?
  // Given the size of the header and payload (17 bytes total), this is most
  // likely the case the vast majority of the time.
  if (db->Remaining() == kOpaqueSize && total_length == kOpaqueSize) {
    // Special case this situation as it allows us to avoid any copying;
    // the other path makes two copies, first into the buffer in
    // QuicHttpStructureDecoder as it accumulates the 8 bytes of opaque data,
    // and a second copy into the QuicHttpPingFields member of in this class.
    // This supports the claim that this decoder is (mostly) non-buffering.
    static_assert(sizeof(QuicHttpPingFields) == kOpaqueSize,
                  "If not, then can't enter this block!");
    auto* ping = reinterpret_cast<const QuicHttpPingFields*>(db->cursor());
    if (frame_header.IsAck()) {
      state->listener()->OnPingAck(frame_header, *ping);
    } else {
      state->listener()->OnPing(frame_header, *ping);
    }
    db->AdvanceCursor(kOpaqueSize);
    return QuicHttpDecodeStatus::kDecodeDone;
  }
  state->InitializeRemainders();
  return HandleStatus(
      state, state->StartDecodingStructureInPayload(&ping_fields_, db));
}

QuicHttpDecodeStatus QuicHttpPingQuicHttpPayloadDecoder::ResumeDecodingPayload(
    QuicHttpFrameDecoderState* state,
    QuicHttpDecodeBuffer* db) {
  DVLOG(2) << "ResumeDecodingPayload: remaining_payload="
           << state->remaining_payload();
  DCHECK_EQ(QuicHttpFrameType::PING, state->frame_header().type);
  DCHECK_LE(db->Remaining(), state->frame_header().payload_length);
  return HandleStatus(
      state, state->ResumeDecodingStructureInPayload(&ping_fields_, db));
}

QuicHttpDecodeStatus QuicHttpPingQuicHttpPayloadDecoder::HandleStatus(
    QuicHttpFrameDecoderState* state,
    QuicHttpDecodeStatus status) {
  DVLOG(2) << "HandleStatus: status=" << status
           << "; remaining_payload=" << state->remaining_payload();
  if (status == QuicHttpDecodeStatus::kDecodeDone) {
    if (state->remaining_payload() == 0) {
      const QuicHttpFrameHeader& frame_header = state->frame_header();
      if (frame_header.IsAck()) {
        state->listener()->OnPingAck(frame_header, ping_fields_);
      } else {
        state->listener()->OnPing(frame_header, ping_fields_);
      }
      return QuicHttpDecodeStatus::kDecodeDone;
    }
    // Payload is too long.
    return state->ReportFrameSizeError();
  }
  // Not done decoding the structure. Either we've got more payload to decode,
  // or we've run out because the payload is too short.
  DCHECK((status == QuicHttpDecodeStatus::kDecodeInProgress &&
          state->remaining_payload() > 0) ||
         (status == QuicHttpDecodeStatus::kDecodeError &&
          state->remaining_payload() == 0))
      << "\n status=" << status
      << "; remaining_payload=" << state->remaining_payload();
  return status;
}

}  // namespace net
