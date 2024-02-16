// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_FRAMES_QUIC_FRAME_H_
#define QUICHE_QUIC_CORE_FRAMES_QUIC_FRAME_H_

#include <ostream>
#include <type_traits>
#include <vector>

#include "absl/container/inlined_vector.h"
#include "quiche/quic/core/frames/quic_ack_frame.h"
#include "quiche/quic/core/frames/quic_ack_frequency_frame.h"
#include "quiche/quic/core/frames/quic_blocked_frame.h"
#include "quiche/quic/core/frames/quic_connection_close_frame.h"
#include "quiche/quic/core/frames/quic_crypto_frame.h"
#include "quiche/quic/core/frames/quic_goaway_frame.h"
#include "quiche/quic/core/frames/quic_handshake_done_frame.h"
#include "quiche/quic/core/frames/quic_max_streams_frame.h"
#include "quiche/quic/core/frames/quic_message_frame.h"
#include "quiche/quic/core/frames/quic_mtu_discovery_frame.h"
#include "quiche/quic/core/frames/quic_new_connection_id_frame.h"
#include "quiche/quic/core/frames/quic_new_token_frame.h"
#include "quiche/quic/core/frames/quic_padding_frame.h"
#include "quiche/quic/core/frames/quic_path_challenge_frame.h"
#include "quiche/quic/core/frames/quic_path_response_frame.h"
#include "quiche/quic/core/frames/quic_ping_frame.h"
#include "quiche/quic/core/frames/quic_retire_connection_id_frame.h"
#include "quiche/quic/core/frames/quic_rst_stream_frame.h"
#include "quiche/quic/core/frames/quic_stop_sending_frame.h"
#include "quiche/quic/core/frames/quic_stop_waiting_frame.h"
#include "quiche/quic/core/frames/quic_stream_frame.h"
#include "quiche/quic/core/frames/quic_streams_blocked_frame.h"
#include "quiche/quic/core/frames/quic_window_update_frame.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_export.h"

#ifndef QUIC_FRAME_DEBUG
#if !defined(NDEBUG) || defined(ADDRESS_SANITIZER)
#define QUIC_FRAME_DEBUG 1
#else  // !defined(NDEBUG) || defined(ADDRESS_SANITIZER)
#define QUIC_FRAME_DEBUG 0
#endif  // !defined(NDEBUG) || defined(ADDRESS_SANITIZER)
#endif  // QUIC_FRAME_DEBUG

namespace quic {

struct QUICHE_EXPORT QuicFrame {
  QuicFrame();
  // Please keep the constructors in the same order as the union below.
  explicit QuicFrame(QuicPaddingFrame padding_frame);
  explicit QuicFrame(QuicMtuDiscoveryFrame frame);
  explicit QuicFrame(QuicPingFrame frame);
  explicit QuicFrame(QuicMaxStreamsFrame frame);
  explicit QuicFrame(QuicStopWaitingFrame frame);
  explicit QuicFrame(QuicStreamsBlockedFrame frame);
  explicit QuicFrame(QuicStreamFrame stream_frame);
  explicit QuicFrame(QuicHandshakeDoneFrame handshake_done_frame);
  explicit QuicFrame(QuicWindowUpdateFrame frame);
  explicit QuicFrame(QuicBlockedFrame frame);
  explicit QuicFrame(QuicStopSendingFrame frame);
  explicit QuicFrame(QuicPathChallengeFrame frame);
  explicit QuicFrame(QuicPathResponseFrame frame);

  explicit QuicFrame(QuicAckFrame* frame);
  explicit QuicFrame(QuicRstStreamFrame* frame);
  explicit QuicFrame(QuicConnectionCloseFrame* frame);
  explicit QuicFrame(QuicGoAwayFrame* frame);
  explicit QuicFrame(QuicNewConnectionIdFrame* frame);
  explicit QuicFrame(QuicRetireConnectionIdFrame* frame);
  explicit QuicFrame(QuicNewTokenFrame* frame);
  explicit QuicFrame(QuicMessageFrame* message_frame);
  explicit QuicFrame(QuicCryptoFrame* crypto_frame);
  explicit QuicFrame(QuicAckFrequencyFrame* ack_frequency_frame);

  QUICHE_EXPORT friend std::ostream& operator<<(std::ostream& os,
                                                const QuicFrame& frame);

  union {
    // Inlined frames.
    // Overlapping inlined frames have a |type| field at the same 0 offset as
    // QuicFrame does for out of line frames below, allowing use of the
    // remaining 7 bytes after offset for frame-type specific fields.
    QuicPaddingFrame padding_frame;
    QuicMtuDiscoveryFrame mtu_discovery_frame;
    QuicPingFrame ping_frame;
    QuicMaxStreamsFrame max_streams_frame;
    QuicStopWaitingFrame stop_waiting_frame;
    QuicStreamsBlockedFrame streams_blocked_frame;
    QuicStreamFrame stream_frame;
    QuicHandshakeDoneFrame handshake_done_frame;
    QuicWindowUpdateFrame window_update_frame;
    QuicBlockedFrame blocked_frame;
    QuicStopSendingFrame stop_sending_frame;
    QuicPathChallengeFrame path_challenge_frame;
    QuicPathResponseFrame path_response_frame;

    // Out of line frames.
    struct {
      QuicFrameType type;

#if QUIC_FRAME_DEBUG
      bool delete_forbidden = false;
#endif  // QUIC_FRAME_DEBUG

      union {
        QuicAckFrame* ack_frame;
        QuicRstStreamFrame* rst_stream_frame;
        QuicConnectionCloseFrame* connection_close_frame;
        QuicGoAwayFrame* goaway_frame;
        QuicNewConnectionIdFrame* new_connection_id_frame;
        QuicRetireConnectionIdFrame* retire_connection_id_frame;
        QuicMessageFrame* message_frame;
        QuicCryptoFrame* crypto_frame;
        QuicAckFrequencyFrame* ack_frequency_frame;
        QuicNewTokenFrame* new_token_frame;
      };
    };
  };
};

static_assert(std::is_standard_layout<QuicFrame>::value,
              "QuicFrame must have a standard layout");
static_assert(sizeof(QuicFrame) <= 24,
              "Frames larger than 24 bytes should be referenced by pointer.");
static_assert(offsetof(QuicStreamFrame, type) == offsetof(QuicFrame, type),
              "Offset of |type| must match in QuicFrame and QuicStreamFrame");

// A inline size of 1 is chosen to optimize the typical use case of
// 1-stream-frame in QuicTransmissionInfo.retransmittable_frames.
using QuicFrames = absl::InlinedVector<QuicFrame, 1>;

// Deletes all the sub-frames contained in |frames|.
QUICHE_EXPORT void DeleteFrames(QuicFrames* frames);

// Delete the sub-frame contained in |frame|.
QUICHE_EXPORT void DeleteFrame(QuicFrame* frame);

// Deletes all the QuicStreamFrames for the specified |stream_id|.
QUICHE_EXPORT void RemoveFramesForStream(QuicFrames* frames,
                                         QuicStreamId stream_id);

// Returns true if |type| is a retransmittable control frame.
QUICHE_EXPORT bool IsControlFrame(QuicFrameType type);

// Returns control_frame_id of |frame|. Returns kInvalidControlFrameId if
// |frame| does not have a valid control_frame_id.
QUICHE_EXPORT QuicControlFrameId GetControlFrameId(const QuicFrame& frame);

// Sets control_frame_id of |frame| to |control_frame_id|.
QUICHE_EXPORT void SetControlFrameId(QuicControlFrameId control_frame_id,
                                     QuicFrame* frame);

// Returns a copy of |frame|.
QUICHE_EXPORT QuicFrame CopyRetransmittableControlFrame(const QuicFrame& frame);

// Returns a copy of |frame|.
QUICHE_EXPORT QuicFrame CopyQuicFrame(quiche::QuicheBufferAllocator* allocator,
                                      const QuicFrame& frame);

// Returns a copy of |frames|.
QUICHE_EXPORT QuicFrames CopyQuicFrames(
    quiche::QuicheBufferAllocator* allocator, const QuicFrames& frames);

// Human-readable description suitable for logging.
QUICHE_EXPORT std::string QuicFrameToString(const QuicFrame& frame);
QUICHE_EXPORT std::string QuicFramesToString(const QuicFrames& frames);

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_FRAMES_QUIC_FRAME_H_
