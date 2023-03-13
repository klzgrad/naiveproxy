// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_PATH_VALIDATOR_H_
#define QUICHE_QUIC_CORE_QUIC_PATH_VALIDATOR_H_

#include <ostream>

#include "absl/container/inlined_vector.h"
#include "quiche/quic/core/crypto/quic_random.h"
#include "quiche/quic/core/quic_alarm.h"
#include "quiche/quic/core/quic_alarm_factory.h"
#include "quiche/quic/core/quic_arena_scoped_ptr.h"
#include "quiche/quic/core/quic_clock.h"
#include "quiche/quic/core/quic_connection_context.h"
#include "quiche/quic/core/quic_one_block_arena.h"
#include "quiche/quic/core/quic_packet_writer.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/quic/platform/api/quic_socket_address.h"

namespace quic {

namespace test {
class QuicPathValidatorPeer;
}

class QuicConnection;

enum class PathValidationReason {
  kReasonUnknown,
  kMultiPort,
  kReversePathValidation,
  kServerPreferredAddressMigration,
  kPortMigration,
  kConnectionMigration,
  kMaxValue,
};

// Interface to provide the information of the path to be validated.
class QUIC_EXPORT_PRIVATE QuicPathValidationContext {
 public:
  QuicPathValidationContext(const QuicSocketAddress& self_address,
                            const QuicSocketAddress& peer_address)
      : self_address_(self_address),
        peer_address_(peer_address),
        effective_peer_address_(peer_address) {}

  QuicPathValidationContext(const QuicSocketAddress& self_address,
                            const QuicSocketAddress& peer_address,
                            const QuicSocketAddress& effective_peer_address)
      : self_address_(self_address),
        peer_address_(peer_address),
        effective_peer_address_(effective_peer_address) {}

  virtual ~QuicPathValidationContext() = default;

  virtual QuicPacketWriter* WriterToUse() = 0;

  const QuicSocketAddress& self_address() const { return self_address_; }
  const QuicSocketAddress& peer_address() const { return peer_address_; }
  const QuicSocketAddress& effective_peer_address() const {
    return effective_peer_address_;
  }

 private:
  QUIC_EXPORT_PRIVATE friend std::ostream& operator<<(
      std::ostream& os, const QuicPathValidationContext& context);

  QuicSocketAddress self_address_;
  // The address to send PATH_CHALLENGE.
  QuicSocketAddress peer_address_;
  // The actual peer address which is different from |peer_address_| if the peer
  // is behind a proxy.
  QuicSocketAddress effective_peer_address_;
};

// Used to validate a path by sending up to 3 PATH_CHALLENGE frames before
// declaring a path validation failure.
class QUIC_EXPORT_PRIVATE QuicPathValidator {
 public:
  static const uint16_t kMaxRetryTimes = 2;

  // Used to write PATH_CHALLENGE on the path to be validated and to get retry
  // timeout.
  class QUIC_EXPORT_PRIVATE SendDelegate {
   public:
    virtual ~SendDelegate() = default;

    // Send a PATH_CHALLENGE with |data_buffer| as the frame payload using given
    // path information. Return false if the delegate doesn't want to continue
    // the validation.
    virtual bool SendPathChallenge(
        const QuicPathFrameBuffer& data_buffer,
        const QuicSocketAddress& self_address,
        const QuicSocketAddress& peer_address,
        const QuicSocketAddress& effective_peer_address,
        QuicPacketWriter* writer) = 0;
    // Return the time to retry sending PATH_CHALLENGE again based on given peer
    // address and writer.
    virtual QuicTime GetRetryTimeout(const QuicSocketAddress& peer_address,
                                     QuicPacketWriter* writer) const = 0;
  };

  // Handles the validation result.
  // TODO(danzh) consider to simplify this interface and its life time to
  // outlive a validation.
  class QUIC_EXPORT_PRIVATE ResultDelegate {
   public:
    virtual ~ResultDelegate() = default;

    // Called when a PATH_RESPONSE is received with a matching PATH_CHALLANGE.
    // |start_time| is the time when the matching PATH_CHALLANGE was sent.
    virtual void OnPathValidationSuccess(
        std::unique_ptr<QuicPathValidationContext> context,
        QuicTime start_time) = 0;

    virtual void OnPathValidationFailure(
        std::unique_ptr<QuicPathValidationContext> context) = 0;
  };

  QuicPathValidator(QuicAlarmFactory* alarm_factory, QuicConnectionArena* arena,
                    SendDelegate* delegate, QuicRandom* random,
                    const QuicClock* clock, QuicConnectionContext* context);

  // Send PATH_CHALLENGE and start the retry timer.
  void StartPathValidation(std::unique_ptr<QuicPathValidationContext> context,
                           std::unique_ptr<ResultDelegate> result_delegate,
                           PathValidationReason reason);

  // Called when a PATH_RESPONSE frame has been received. Matches the received
  // PATH_RESPONSE payload with the payloads previously sent in PATH_CHALLANGE
  // frames and the self address on which it was sent.
  void OnPathResponse(const QuicPathFrameBuffer& probing_data,
                      QuicSocketAddress self_address);

  // Cancel the retry timer and reset the path and result delegate.
  void CancelPathValidation();

  bool HasPendingPathValidation() const;

  QuicPathValidationContext* GetContext() const;

  PathValidationReason GetPathValidationReason() const { return reason_; }

  // Send another PATH_CHALLENGE on the same path. After retrying
  // |kMaxRetryTimes| times, fail the current path validation.
  void OnRetryTimeout();

  bool IsValidatingPeerAddress(const QuicSocketAddress& effective_peer_address);

  // Called to send packet to |peer_address| if the path validation to this
  // address is pending.
  void MaybeWritePacketToAddress(const char* buffer, size_t buf_len,
                                 const QuicSocketAddress& peer_address);

 private:
  friend class test::QuicPathValidatorPeer;

  // Return the payload to be used in the next PATH_CHALLENGE frame.
  const QuicPathFrameBuffer& GeneratePathChallengePayload();

  void SendPathChallengeAndSetAlarm();

  void ResetPathValidation();

  struct QUIC_NO_EXPORT ProbingData {
    explicit ProbingData(QuicTime send_time) : send_time(send_time) {}
    QuicPathFrameBuffer frame_buffer;
    QuicTime send_time;
  };

  // Has at most 3 entries due to validation timeout.
  absl::InlinedVector<ProbingData, 3> probing_data_;
  SendDelegate* send_delegate_;
  QuicRandom* random_;
  const QuicClock* clock_;
  std::unique_ptr<QuicPathValidationContext> path_context_;
  std::unique_ptr<ResultDelegate> result_delegate_;
  QuicArenaScopedPtr<QuicAlarm> retry_timer_;
  size_t retry_count_;
  PathValidationReason reason_ = PathValidationReason::kReasonUnknown;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_PATH_VALIDATOR_H_
