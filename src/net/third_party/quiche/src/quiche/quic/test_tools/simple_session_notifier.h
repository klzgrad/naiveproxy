// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_SIMPLE_SESSION_NOTIFIER_H_
#define QUICHE_QUIC_TEST_TOOLS_SIMPLE_SESSION_NOTIFIER_H_

#include "absl/container/flat_hash_map.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_interval_set.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/session_notifier_interface.h"
#include "quiche/quic/platform/api/quic_test.h"
#include "quiche/common/quiche_circular_deque.h"
#include "quiche/common/quiche_linked_hash_map.h"

namespace quic {

class QuicConnection;

namespace test {

// SimpleSessionNotifier implements the basic functionalities of a session, and
// it manages stream data and control frames.
class SimpleSessionNotifier : public SessionNotifierInterface {
 public:
  explicit SimpleSessionNotifier(QuicConnection* connection);
  ~SimpleSessionNotifier() override;

  // Tries to write stream data and returns data consumed.
  QuicConsumedData WriteOrBufferData(QuicStreamId id, QuicByteCount data_length,
                                     StreamSendingState state);
  QuicConsumedData WriteOrBufferData(QuicStreamId id, QuicByteCount data_length,
                                     StreamSendingState state,
                                     TransmissionType transmission_type);

  // Tries to write RST_STREAM_FRAME.
  void WriteOrBufferRstStream(QuicStreamId id, QuicRstStreamErrorCode error,
                              QuicStreamOffset bytes_written);

  // Tries to write RESET_STREAM_AT_FRAME.
  void WriteOrBufferResetStreamAt(QuicStreamId id, QuicRstStreamErrorCode error,
                                  QuicStreamOffset bytes_written,
                                  QuicStreamOffset reliable_size);

  // Tries to write WINDOW_UPDATE.
  void WriteOrBufferWindowUpate(QuicStreamId id, QuicStreamOffset byte_offset);

  // Tries to write PING.
  void WriteOrBufferPing();

  // Tries to write ACK_FREQUENCY.
  void WriteOrBufferAckFrequency(
      const QuicAckFrequencyFrame& ack_frequency_frame);

  // Tries to write CRYPTO data and returns the number of bytes written.
  size_t WriteCryptoData(EncryptionLevel level, QuicByteCount data_length,
                         QuicStreamOffset offset);

  // Neuters unencrypted data of crypto stream.
  void NeuterUnencryptedData();

  // Called when connection_ becomes writable.
  void OnCanWrite();

  // Called to reset stream.
  void OnStreamReset(QuicStreamId id, QuicRstStreamErrorCode error);

  // Returns true if there are 1) unsent control frames and stream data, or 2)
  // lost control frames and stream data.
  bool WillingToWrite() const;

  // Number of sent stream bytes. Please note, this does not count
  // retransmissions.
  QuicByteCount StreamBytesSent() const;

  // Number of stream bytes waiting to be sent for the first time.
  QuicByteCount StreamBytesToSend() const;

  // Returns true if there is any stream data waiting to be sent for the first
  // time.
  bool HasBufferedStreamData() const;

  // Returns true if stream |id| has any outstanding data.
  bool StreamIsWaitingForAcks(QuicStreamId id) const;

  // SessionNotifierInterface methods:
  bool OnFrameAcked(const QuicFrame& frame, QuicTime::Delta ack_delay_time,
                    QuicTime receive_timestamp) override;
  void OnStreamFrameRetransmitted(const QuicStreamFrame& /*frame*/) override {}
  void OnFrameLost(const QuicFrame& frame) override;
  bool RetransmitFrames(const QuicFrames& frames,
                        TransmissionType type) override;
  bool IsFrameOutstanding(const QuicFrame& frame) const override;
  bool HasUnackedCryptoData() const override;
  bool HasUnackedStreamData() const override;
  bool HasLostStreamData() const;

 private:
  struct StreamState {
    StreamState();
    ~StreamState();

    // Total number of bytes.
    QuicByteCount bytes_total;
    // Number of sent bytes.
    QuicByteCount bytes_sent;
    // Record of acked offsets.
    QuicIntervalSet<QuicStreamOffset> bytes_acked;
    // Data considered as lost and needs to be retransmitted.
    QuicIntervalSet<QuicStreamOffset> pending_retransmissions;

    bool fin_buffered;
    bool fin_sent;
    bool fin_outstanding;
    bool fin_lost;
  };

  friend std::ostream& operator<<(std::ostream& os, const StreamState& s);

  using StreamMap = absl::flat_hash_map<QuicStreamId, StreamState>;

  void OnStreamDataConsumed(QuicStreamId id, QuicStreamOffset offset,
                            QuicByteCount data_length, bool fin);

  bool OnControlFrameAcked(const QuicFrame& frame);

  void OnControlFrameLost(const QuicFrame& frame);

  bool RetransmitLostControlFrames();

  bool RetransmitLostCryptoData();

  bool RetransmitLostStreamData();

  bool WriteBufferedControlFrames();

  bool WriteBufferedCryptoData();

  bool IsControlFrameOutstanding(const QuicFrame& frame) const;

  bool HasBufferedControlFrames() const;

  bool StreamHasBufferedData(QuicStreamId id) const;

  quiche::QuicheCircularDeque<QuicFrame> control_frames_;

  quiche::QuicheLinkedHashMap<QuicControlFrameId, bool> lost_control_frames_;

  // Id of latest saved control frame. 0 if no control frame has been saved.
  QuicControlFrameId last_control_frame_id_;

  // The control frame at the 0th index of control_frames_.
  QuicControlFrameId least_unacked_;

  // ID of the least unsent control frame.
  QuicControlFrameId least_unsent_;

  StreamMap stream_map_;

  // Transferred crypto bytes according to encryption levels.
  QuicIntervalSet<QuicStreamOffset>
      crypto_bytes_transferred_[NUM_ENCRYPTION_LEVELS];

  StreamState crypto_state_[NUM_ENCRYPTION_LEVELS];

  QuicConnection* connection_;
};

}  // namespace test

}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_SIMPLE_SESSION_NOTIFIER_H_
