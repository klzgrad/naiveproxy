// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/frames/quic_frame.h"

#include "quiche/quic/core/frames/quic_new_connection_id_frame.h"
#include "quiche/quic/core/frames/quic_reset_stream_at_frame.h"
#include "quiche/quic/core/frames/quic_retire_connection_id_frame.h"
#include "quiche/quic/core/frames/quic_rst_stream_frame.h"
#include "quiche/quic/core/quic_constants.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/common/platform/api/quiche_mem_slice.h"
#include "quiche/common/quiche_buffer_allocator.h"

namespace quic {

QuicFrame::QuicFrame() {}

QuicFrame::QuicFrame(QuicPaddingFrame padding_frame)
    : padding_frame(padding_frame) {}

QuicFrame::QuicFrame(QuicStreamFrame stream_frame)
    : stream_frame(stream_frame) {}

QuicFrame::QuicFrame(QuicHandshakeDoneFrame handshake_done_frame)
    : handshake_done_frame(handshake_done_frame) {}

QuicFrame::QuicFrame(QuicCryptoFrame* crypto_frame)
    : type(CRYPTO_FRAME), crypto_frame(crypto_frame) {}

QuicFrame::QuicFrame(QuicAckFrame* frame) : type(ACK_FRAME), ack_frame(frame) {}

QuicFrame::QuicFrame(QuicMtuDiscoveryFrame frame)
    : mtu_discovery_frame(frame) {}

QuicFrame::QuicFrame(QuicStopWaitingFrame frame) : stop_waiting_frame(frame) {}

QuicFrame::QuicFrame(QuicPingFrame frame) : ping_frame(frame) {}

QuicFrame::QuicFrame(QuicRstStreamFrame* frame)
    : type(RST_STREAM_FRAME), rst_stream_frame(frame) {}

QuicFrame::QuicFrame(QuicConnectionCloseFrame* frame)
    : type(CONNECTION_CLOSE_FRAME), connection_close_frame(frame) {}

QuicFrame::QuicFrame(QuicGoAwayFrame* frame)
    : type(GOAWAY_FRAME), goaway_frame(frame) {}

QuicFrame::QuicFrame(QuicWindowUpdateFrame frame)
    : window_update_frame(frame) {}

QuicFrame::QuicFrame(QuicBlockedFrame frame) : blocked_frame(frame) {}

QuicFrame::QuicFrame(QuicNewConnectionIdFrame* frame)
    : type(NEW_CONNECTION_ID_FRAME), new_connection_id_frame(frame) {}

QuicFrame::QuicFrame(QuicRetireConnectionIdFrame* frame)
    : type(RETIRE_CONNECTION_ID_FRAME), retire_connection_id_frame(frame) {}

QuicFrame::QuicFrame(QuicMaxStreamsFrame frame) : max_streams_frame(frame) {}

QuicFrame::QuicFrame(QuicStreamsBlockedFrame frame)
    : streams_blocked_frame(frame) {}

QuicFrame::QuicFrame(QuicPathResponseFrame frame)
    : path_response_frame(frame) {}

QuicFrame::QuicFrame(QuicPathChallengeFrame frame)
    : path_challenge_frame(frame) {}

QuicFrame::QuicFrame(QuicStopSendingFrame frame) : stop_sending_frame(frame) {}

QuicFrame::QuicFrame(QuicMessageFrame* frame)
    : type(MESSAGE_FRAME), message_frame(frame) {}

QuicFrame::QuicFrame(QuicNewTokenFrame* frame)
    : type(NEW_TOKEN_FRAME), new_token_frame(frame) {}

QuicFrame::QuicFrame(QuicAckFrequencyFrame* frame)
    : type(ACK_FREQUENCY_FRAME), ack_frequency_frame(frame) {}

QuicFrame::QuicFrame(QuicResetStreamAtFrame* frame)
    : type(RESET_STREAM_AT_FRAME), reset_stream_at_frame(frame) {}

void DeleteFrames(QuicFrames* frames) {
  for (QuicFrame& frame : *frames) {
    DeleteFrame(&frame);
  }
  frames->clear();
}

void DeleteFrame(QuicFrame* frame) {
#if QUIC_FRAME_DEBUG
  // If the frame is not inlined, check that it can be safely deleted.
  if (frame->type != PADDING_FRAME && frame->type != MTU_DISCOVERY_FRAME &&
      frame->type != PING_FRAME && frame->type != MAX_STREAMS_FRAME &&
      frame->type != STOP_WAITING_FRAME &&
      frame->type != STREAMS_BLOCKED_FRAME && frame->type != STREAM_FRAME &&
      frame->type != HANDSHAKE_DONE_FRAME &&
      frame->type != WINDOW_UPDATE_FRAME && frame->type != BLOCKED_FRAME &&
      frame->type != STOP_SENDING_FRAME &&
      frame->type != PATH_CHALLENGE_FRAME &&
      frame->type != PATH_RESPONSE_FRAME) {
    QUICHE_CHECK(!frame->delete_forbidden) << *frame;
  }
#endif  // QUIC_FRAME_DEBUG
  switch (frame->type) {
    // Frames smaller than a pointer are inlined, so don't need to be deleted.
    case PADDING_FRAME:
    case MTU_DISCOVERY_FRAME:
    case PING_FRAME:
    case MAX_STREAMS_FRAME:
    case STOP_WAITING_FRAME:
    case STREAMS_BLOCKED_FRAME:
    case STREAM_FRAME:
    case HANDSHAKE_DONE_FRAME:
    case WINDOW_UPDATE_FRAME:
    case BLOCKED_FRAME:
    case STOP_SENDING_FRAME:
    case PATH_CHALLENGE_FRAME:
    case PATH_RESPONSE_FRAME:
      break;
    case ACK_FRAME:
      delete frame->ack_frame;
      break;
    case RST_STREAM_FRAME:
      delete frame->rst_stream_frame;
      break;
    case CONNECTION_CLOSE_FRAME:
      delete frame->connection_close_frame;
      break;
    case GOAWAY_FRAME:
      delete frame->goaway_frame;
      break;
    case NEW_CONNECTION_ID_FRAME:
      delete frame->new_connection_id_frame;
      break;
    case RETIRE_CONNECTION_ID_FRAME:
      delete frame->retire_connection_id_frame;
      break;
    case MESSAGE_FRAME:
      delete frame->message_frame;
      break;
    case CRYPTO_FRAME:
      delete frame->crypto_frame;
      break;
    case NEW_TOKEN_FRAME:
      delete frame->new_token_frame;
      break;
    case ACK_FREQUENCY_FRAME:
      delete frame->ack_frequency_frame;
      break;
    case RESET_STREAM_AT_FRAME:
      delete frame->reset_stream_at_frame;
      break;
    case NUM_FRAME_TYPES:
      QUICHE_DCHECK(false) << "Cannot delete type: " << frame->type;
  }
}

void RemoveFramesForStream(QuicFrames* frames, QuicStreamId stream_id) {
  auto it = frames->begin();
  while (it != frames->end()) {
    if (it->type != STREAM_FRAME || it->stream_frame.stream_id != stream_id) {
      ++it;
      continue;
    }
    it = frames->erase(it);
  }
}

bool IsControlFrame(QuicFrameType type) {
  switch (type) {
    case RST_STREAM_FRAME:
    case GOAWAY_FRAME:
    case WINDOW_UPDATE_FRAME:
    case BLOCKED_FRAME:
    case STREAMS_BLOCKED_FRAME:
    case MAX_STREAMS_FRAME:
    case PING_FRAME:
    case STOP_SENDING_FRAME:
    case NEW_CONNECTION_ID_FRAME:
    case RETIRE_CONNECTION_ID_FRAME:
    case HANDSHAKE_DONE_FRAME:
    case ACK_FREQUENCY_FRAME:
    case NEW_TOKEN_FRAME:
    case RESET_STREAM_AT_FRAME:
      return true;
    default:
      return false;
  }
}

QuicControlFrameId GetControlFrameId(const QuicFrame& frame) {
  switch (frame.type) {
    case RST_STREAM_FRAME:
      return frame.rst_stream_frame->control_frame_id;
    case GOAWAY_FRAME:
      return frame.goaway_frame->control_frame_id;
    case WINDOW_UPDATE_FRAME:
      return frame.window_update_frame.control_frame_id;
    case BLOCKED_FRAME:
      return frame.blocked_frame.control_frame_id;
    case STREAMS_BLOCKED_FRAME:
      return frame.streams_blocked_frame.control_frame_id;
    case MAX_STREAMS_FRAME:
      return frame.max_streams_frame.control_frame_id;
    case PING_FRAME:
      return frame.ping_frame.control_frame_id;
    case STOP_SENDING_FRAME:
      return frame.stop_sending_frame.control_frame_id;
    case NEW_CONNECTION_ID_FRAME:
      return frame.new_connection_id_frame->control_frame_id;
    case RETIRE_CONNECTION_ID_FRAME:
      return frame.retire_connection_id_frame->control_frame_id;
    case HANDSHAKE_DONE_FRAME:
      return frame.handshake_done_frame.control_frame_id;
    case ACK_FREQUENCY_FRAME:
      return frame.ack_frequency_frame->control_frame_id;
    case NEW_TOKEN_FRAME:
      return frame.new_token_frame->control_frame_id;
    case RESET_STREAM_AT_FRAME:
      return frame.reset_stream_at_frame->control_frame_id;
    default:
      return kInvalidControlFrameId;
  }
}

void SetControlFrameId(QuicControlFrameId control_frame_id, QuicFrame* frame) {
  switch (frame->type) {
    case RST_STREAM_FRAME:
      frame->rst_stream_frame->control_frame_id = control_frame_id;
      return;
    case GOAWAY_FRAME:
      frame->goaway_frame->control_frame_id = control_frame_id;
      return;
    case WINDOW_UPDATE_FRAME:
      frame->window_update_frame.control_frame_id = control_frame_id;
      return;
    case BLOCKED_FRAME:
      frame->blocked_frame.control_frame_id = control_frame_id;
      return;
    case PING_FRAME:
      frame->ping_frame.control_frame_id = control_frame_id;
      return;
    case STREAMS_BLOCKED_FRAME:
      frame->streams_blocked_frame.control_frame_id = control_frame_id;
      return;
    case MAX_STREAMS_FRAME:
      frame->max_streams_frame.control_frame_id = control_frame_id;
      return;
    case STOP_SENDING_FRAME:
      frame->stop_sending_frame.control_frame_id = control_frame_id;
      return;
    case NEW_CONNECTION_ID_FRAME:
      frame->new_connection_id_frame->control_frame_id = control_frame_id;
      return;
    case RETIRE_CONNECTION_ID_FRAME:
      frame->retire_connection_id_frame->control_frame_id = control_frame_id;
      return;
    case HANDSHAKE_DONE_FRAME:
      frame->handshake_done_frame.control_frame_id = control_frame_id;
      return;
    case ACK_FREQUENCY_FRAME:
      frame->ack_frequency_frame->control_frame_id = control_frame_id;
      return;
    case NEW_TOKEN_FRAME:
      frame->new_token_frame->control_frame_id = control_frame_id;
      return;
    case RESET_STREAM_AT_FRAME:
      frame->reset_stream_at_frame->control_frame_id = control_frame_id;
      return;
    default:
      QUIC_BUG(quic_bug_12594_1)
          << "Try to set control frame id of a frame without control frame id";
  }
}

QuicFrame CopyRetransmittableControlFrame(const QuicFrame& frame) {
  QuicFrame copy;
  switch (frame.type) {
    case RST_STREAM_FRAME:
      copy = QuicFrame(new QuicRstStreamFrame(*frame.rst_stream_frame));
      break;
    case GOAWAY_FRAME:
      copy = QuicFrame(new QuicGoAwayFrame(*frame.goaway_frame));
      break;
    case WINDOW_UPDATE_FRAME:
      copy = QuicFrame(QuicWindowUpdateFrame(frame.window_update_frame));
      break;
    case BLOCKED_FRAME:
      copy = QuicFrame(QuicBlockedFrame(frame.blocked_frame));
      break;
    case PING_FRAME:
      copy = QuicFrame(QuicPingFrame(frame.ping_frame.control_frame_id));
      break;
    case STOP_SENDING_FRAME:
      copy = QuicFrame(QuicStopSendingFrame(frame.stop_sending_frame));
      break;
    case NEW_CONNECTION_ID_FRAME:
      copy = QuicFrame(
          new QuicNewConnectionIdFrame(*frame.new_connection_id_frame));
      break;
    case RETIRE_CONNECTION_ID_FRAME:
      copy = QuicFrame(
          new QuicRetireConnectionIdFrame(*frame.retire_connection_id_frame));
      break;
    case STREAMS_BLOCKED_FRAME:
      copy = QuicFrame(QuicStreamsBlockedFrame(frame.streams_blocked_frame));
      break;
    case MAX_STREAMS_FRAME:
      copy = QuicFrame(QuicMaxStreamsFrame(frame.max_streams_frame));
      break;
    case HANDSHAKE_DONE_FRAME:
      copy = QuicFrame(
          QuicHandshakeDoneFrame(frame.handshake_done_frame.control_frame_id));
      break;
    case ACK_FREQUENCY_FRAME:
      copy = QuicFrame(new QuicAckFrequencyFrame(*frame.ack_frequency_frame));
      break;
    case NEW_TOKEN_FRAME:
      copy = QuicFrame(new QuicNewTokenFrame(*frame.new_token_frame));
      break;
    case RESET_STREAM_AT_FRAME:
      copy =
          QuicFrame(new QuicResetStreamAtFrame(*frame.reset_stream_at_frame));
      break;
    default:
      QUIC_BUG(quic_bug_10533_1)
          << "Try to copy a non-retransmittable control frame: " << frame;
      copy = QuicFrame(QuicPingFrame(kInvalidControlFrameId));
      break;
  }
  return copy;
}

QuicFrame CopyQuicFrame(quiche::QuicheBufferAllocator* allocator,
                        const QuicFrame& frame) {
  QuicFrame copy;
  switch (frame.type) {
    case PADDING_FRAME:
      copy = QuicFrame(QuicPaddingFrame(frame.padding_frame));
      break;
    case RST_STREAM_FRAME:
      copy = QuicFrame(new QuicRstStreamFrame(*frame.rst_stream_frame));
      break;
    case CONNECTION_CLOSE_FRAME:
      copy = QuicFrame(
          new QuicConnectionCloseFrame(*frame.connection_close_frame));
      break;
    case GOAWAY_FRAME:
      copy = QuicFrame(new QuicGoAwayFrame(*frame.goaway_frame));
      break;
    case WINDOW_UPDATE_FRAME:
      copy = QuicFrame(QuicWindowUpdateFrame(frame.window_update_frame));
      break;
    case BLOCKED_FRAME:
      copy = QuicFrame(QuicBlockedFrame(frame.blocked_frame));
      break;
    case STOP_WAITING_FRAME:
      copy = QuicFrame(QuicStopWaitingFrame(frame.stop_waiting_frame));
      break;
    case PING_FRAME:
      copy = QuicFrame(QuicPingFrame(frame.ping_frame.control_frame_id));
      break;
    case CRYPTO_FRAME:
      copy = QuicFrame(new QuicCryptoFrame(*frame.crypto_frame));
      break;
    case STREAM_FRAME:
      copy = QuicFrame(QuicStreamFrame(frame.stream_frame));
      break;
    case ACK_FRAME:
      copy = QuicFrame(new QuicAckFrame(*frame.ack_frame));
      break;
    case MTU_DISCOVERY_FRAME:
      copy = QuicFrame(QuicMtuDiscoveryFrame(frame.mtu_discovery_frame));
      break;
    case NEW_CONNECTION_ID_FRAME:
      copy = QuicFrame(
          new QuicNewConnectionIdFrame(*frame.new_connection_id_frame));
      break;
    case MAX_STREAMS_FRAME:
      copy = QuicFrame(QuicMaxStreamsFrame(frame.max_streams_frame));
      break;
    case STREAMS_BLOCKED_FRAME:
      copy = QuicFrame(QuicStreamsBlockedFrame(frame.streams_blocked_frame));
      break;
    case PATH_RESPONSE_FRAME:
      copy = QuicFrame(QuicPathResponseFrame(frame.path_response_frame));
      break;
    case PATH_CHALLENGE_FRAME:
      copy = QuicFrame(QuicPathChallengeFrame(frame.path_challenge_frame));
      break;
    case STOP_SENDING_FRAME:
      copy = QuicFrame(QuicStopSendingFrame(frame.stop_sending_frame));
      break;
    case MESSAGE_FRAME:
      copy = QuicFrame(new QuicMessageFrame(frame.message_frame->message_id));
      copy.message_frame->data = frame.message_frame->data;
      copy.message_frame->message_length = frame.message_frame->message_length;
      for (const auto& slice : frame.message_frame->message_data) {
        quiche::QuicheBuffer buffer =
            quiche::QuicheBuffer::Copy(allocator, slice.AsStringView());
        copy.message_frame->message_data.push_back(
            quiche::QuicheMemSlice(std::move(buffer)));
      }
      break;
    case NEW_TOKEN_FRAME:
      copy = QuicFrame(new QuicNewTokenFrame(*frame.new_token_frame));
      break;
    case RETIRE_CONNECTION_ID_FRAME:
      copy = QuicFrame(
          new QuicRetireConnectionIdFrame(*frame.retire_connection_id_frame));
      break;
    case HANDSHAKE_DONE_FRAME:
      copy = QuicFrame(
          QuicHandshakeDoneFrame(frame.handshake_done_frame.control_frame_id));
      break;
    case ACK_FREQUENCY_FRAME:
      copy = QuicFrame(new QuicAckFrequencyFrame(*frame.ack_frequency_frame));
      break;
    case RESET_STREAM_AT_FRAME:
      copy =
          QuicFrame(new QuicResetStreamAtFrame(*frame.reset_stream_at_frame));
      break;
    default:
      QUIC_BUG(quic_bug_10533_2) << "Cannot copy frame: " << frame;
      copy = QuicFrame(QuicPingFrame(kInvalidControlFrameId));
      break;
  }
  return copy;
}

QuicFrames CopyQuicFrames(quiche::QuicheBufferAllocator* allocator,
                          const QuicFrames& frames) {
  QuicFrames copy;
  for (const auto& frame : frames) {
    copy.push_back(CopyQuicFrame(allocator, frame));
  }
  return copy;
}

std::ostream& operator<<(std::ostream& os, const QuicFrame& frame) {
  switch (frame.type) {
    case PADDING_FRAME: {
      os << "type { PADDING_FRAME } " << frame.padding_frame;
      break;
    }
    case RST_STREAM_FRAME: {
      os << "type { RST_STREAM_FRAME } " << *(frame.rst_stream_frame);
      break;
    }
    case CONNECTION_CLOSE_FRAME: {
      os << "type { CONNECTION_CLOSE_FRAME } "
         << *(frame.connection_close_frame);
      break;
    }
    case GOAWAY_FRAME: {
      os << "type { GOAWAY_FRAME } " << *(frame.goaway_frame);
      break;
    }
    case WINDOW_UPDATE_FRAME: {
      os << "type { WINDOW_UPDATE_FRAME } " << frame.window_update_frame;
      break;
    }
    case BLOCKED_FRAME: {
      os << "type { BLOCKED_FRAME } " << frame.blocked_frame;
      break;
    }
    case STREAM_FRAME: {
      os << "type { STREAM_FRAME } " << frame.stream_frame;
      break;
    }
    case ACK_FRAME: {
      os << "type { ACK_FRAME } " << *(frame.ack_frame);
      break;
    }
    case STOP_WAITING_FRAME: {
      os << "type { STOP_WAITING_FRAME } " << frame.stop_waiting_frame;
      break;
    }
    case PING_FRAME: {
      os << "type { PING_FRAME } " << frame.ping_frame;
      break;
    }
    case CRYPTO_FRAME: {
      os << "type { CRYPTO_FRAME } " << *(frame.crypto_frame);
      break;
    }
    case MTU_DISCOVERY_FRAME: {
      os << "type { MTU_DISCOVERY_FRAME } ";
      break;
    }
    case NEW_CONNECTION_ID_FRAME:
      os << "type { NEW_CONNECTION_ID } " << *(frame.new_connection_id_frame);
      break;
    case RETIRE_CONNECTION_ID_FRAME:
      os << "type { RETIRE_CONNECTION_ID } "
         << *(frame.retire_connection_id_frame);
      break;
    case MAX_STREAMS_FRAME:
      os << "type { MAX_STREAMS } " << frame.max_streams_frame;
      break;
    case STREAMS_BLOCKED_FRAME:
      os << "type { STREAMS_BLOCKED } " << frame.streams_blocked_frame;
      break;
    case PATH_RESPONSE_FRAME:
      os << "type { PATH_RESPONSE } " << frame.path_response_frame;
      break;
    case PATH_CHALLENGE_FRAME:
      os << "type { PATH_CHALLENGE } " << frame.path_challenge_frame;
      break;
    case STOP_SENDING_FRAME:
      os << "type { STOP_SENDING } " << frame.stop_sending_frame;
      break;
    case MESSAGE_FRAME:
      os << "type { MESSAGE_FRAME }" << *(frame.message_frame);
      break;
    case NEW_TOKEN_FRAME:
      os << "type { NEW_TOKEN_FRAME }" << *(frame.new_token_frame);
      break;
    case HANDSHAKE_DONE_FRAME:
      os << "type { HANDSHAKE_DONE_FRAME } " << frame.handshake_done_frame;
      break;
    case ACK_FREQUENCY_FRAME:
      os << "type { ACK_FREQUENCY_FRAME } " << *(frame.ack_frequency_frame);
      break;
    case RESET_STREAM_AT_FRAME:
      os << "type { RESET_STREAM_AT_FRAME } " << *(frame.reset_stream_at_frame);
      break;
    default: {
      QUIC_LOG(ERROR) << "Unknown frame type: " << frame.type;
      break;
    }
  }
  return os;
}

QUICHE_EXPORT std::string QuicFrameToString(const QuicFrame& frame) {
  std::ostringstream os;
  os << frame;
  return os.str();
}

std::string QuicFramesToString(const QuicFrames& frames) {
  std::ostringstream os;
  for (const QuicFrame& frame : frames) {
    os << frame;
  }
  return os.str();
}

}  // namespace quic
