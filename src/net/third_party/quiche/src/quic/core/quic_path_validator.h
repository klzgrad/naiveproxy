// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_PATH_VALIDATOR_H_
#define QUICHE_QUIC_CORE_QUIC_PATH_VALIDATOR_H_

#include <ostream>

#include "net/third_party/quiche/src/quic/core/crypto/quic_random.h"
#include "net/third_party/quiche/src/quic/core/quic_alarm.h"
#include "net/third_party/quiche/src/quic/core/quic_alarm_factory.h"
#include "net/third_party/quiche/src/quic/core/quic_arena_scoped_ptr.h"
#include "net/third_party/quiche/src/quic/core/quic_clock.h"
#include "net/third_party/quiche/src/quic/core/quic_one_block_arena.h"
#include "net/third_party/quiche/src/quic/core/quic_packet_writer.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_socket_address.h"
#include "net/quic/platform/impl/quic_export_impl.h"

namespace quic {

namespace test {
class QuicPathValidatorPeer;
}

class QuicConnection;

// Interface to provide the information of the path to be validated.
class QUIC_EXPORT_PRIVATE QuicPathValidationContext {
 public:
  QuicPathValidationContext(const QuicSocketAddress& self_address,
                            const QuicSocketAddress& peer_address)
      : self_address_(self_address), peer_address_(peer_address) {}

  virtual ~QuicPathValidationContext() = default;

  virtual QuicPacketWriter* WriterToUse() = 0;

  const QuicSocketAddress& self_address() const { return self_address_; }
  const QuicSocketAddress& peer_address() const { return peer_address_; }

 private:
  QUIC_EXPORT_PRIVATE friend std::ostream& operator<<(
      std::ostream& os,
      const QuicPathValidationContext& context);

  QuicSocketAddress self_address_;
  QuicSocketAddress peer_address_;
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
    virtual bool SendPathChallenge(const QuicPathFrameBuffer& data_buffer,
                                   const QuicSocketAddress& self_address,
                                   const QuicSocketAddress& peer_address,
                                   QuicPacketWriter* writer) = 0;
    // Return the time to retry sending PATH_CHALLENGE again based on given peer
    // address and writer.
    virtual QuicTime GetRetryTimeout(const QuicSocketAddress& peer_address,
                                     QuicPacketWriter* writer) const = 0;
  };

  // Handles the validation result.
  class QUIC_EXPORT_PRIVATE ResultDelegate {
   public:
    virtual ~ResultDelegate() = default;

    virtual void OnPathValidationSuccess(
        std::unique_ptr<QuicPathValidationContext> context) = 0;

    virtual void OnPathValidationFailure(
        std::unique_ptr<QuicPathValidationContext> context) = 0;
  };

  QuicPathValidator(QuicAlarmFactory* alarm_factory,
                    QuicConnectionArena* arena,
                    SendDelegate* delegate,
                    QuicRandom* random);

  // Send PATH_CHALLENGE and start the retry timer.
  void StartValidingPath(std::unique_ptr<QuicPathValidationContext> context,
                         std::unique_ptr<ResultDelegate> result_delegate);

  // Called when a PATH_RESPONSE frame has been received. Matches the received
  // PATH_RESPONSE payload with the payloads previously sent in PATH_CHALLANGE
  // frames and the self address on which it was sent.
  void OnPathResponse(const QuicPathFrameBuffer& probing_data,
                      QuicSocketAddress self_address);

  // Cancel the retry timer and reset the path and result delegate.
  void CancelPathValidation();

  bool HasPendingPathValidation() const;

  // Send another PATH_CHALLENGE on the same path. After retrying
  // |kMaxRetryTimes| times, fail the current path validation.
  void OnRetryTimeout();

 private:
  friend class test::QuicPathValidatorPeer;

  // Return the payload to be used in the next PATH_CHALLENGE frame.
  const QuicPathFrameBuffer& GeneratePathChallengePayload();

  void SendPathChallengeAndSetAlarm();

  void ResetPathValidation();

  // Has at most 3 entries due to validation timeout.
  QuicInlinedVector<QuicPathFrameBuffer, 3> probing_data_;
  SendDelegate* send_delegate_;
  QuicRandom* random_;
  std::unique_ptr<QuicPathValidationContext> path_context_;
  std::unique_ptr<ResultDelegate> result_delegate_;
  QuicArenaScopedPtr<QuicAlarm> retry_timer_;
  size_t retry_count_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_PATH_VALIDATOR_H_
