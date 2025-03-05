// Copyright (c) 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_CHAOS_PROTECTOR_H_
#define QUICHE_QUIC_CORE_QUIC_CHAOS_PROTECTOR_H_

#include <cstddef>
#include <memory>
#include <optional>

#include "quiche/quic/core/crypto/quic_random.h"
#include "quiche/quic/core/frames/quic_crypto_frame.h"
#include "quiche/quic/core/frames/quic_frame.h"
#include "quiche/quic/core/quic_data_writer.h"
#include "quiche/quic/core/quic_framer.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/core/quic_stream_frame_data_producer.h"
#include "quiche/quic/core/quic_types.h"

namespace quic {

namespace test {
class QuicChaosProtectorOldTest;
}

// QuicChaosProtectorOld will take a crypto frame and an amount of padding and
// build a data packet that will parse to something equivalent.
class QUICHE_EXPORT QuicChaosProtectorOld : public QuicStreamFrameDataProducer {
 public:
  // |framer| and |random| must be valid for the lifetime of
  // QuicChaosProtectorOld.
  explicit QuicChaosProtectorOld(const QuicCryptoFrame& crypto_frame,
                                 int num_padding_bytes, size_t packet_size,
                                 QuicFramer* framer, QuicRandom* random);

  ~QuicChaosProtectorOld() override;

  QuicChaosProtectorOld(const QuicChaosProtectorOld&) = delete;
  QuicChaosProtectorOld(QuicChaosProtectorOld&&) = delete;
  QuicChaosProtectorOld& operator=(const QuicChaosProtectorOld&) = delete;
  QuicChaosProtectorOld& operator=(QuicChaosProtectorOld&&) = delete;

  // Attempts to build a data packet with chaos protection. If an error occurs,
  // then std::nullopt is returned. Otherwise returns the serialized length.
  std::optional<size_t> BuildDataPacket(const QuicPacketHeader& header,
                                        char* buffer);

  // From QuicStreamFrameDataProducer.
  WriteStreamDataResult WriteStreamData(QuicStreamId id,
                                        QuicStreamOffset offset,
                                        QuicByteCount data_length,
                                        QuicDataWriter* /*writer*/) override;
  bool WriteCryptoData(EncryptionLevel level, QuicStreamOffset offset,
                       QuicByteCount data_length,
                       QuicDataWriter* writer) override;

 private:
  friend class test::QuicChaosProtectorOldTest;

  // Allocate the crypto data buffer, create the CRYPTO frame and write the
  // crypto data to our buffer.
  bool CopyCryptoDataToLocalBuffer();

  // Split the CRYPTO frame in |frames_| into one or more CRYPTO frames that
  // collectively represent the same data. Adjusts padding to compensate.
  void SplitCryptoFrame();

  // Add a random number of PING frames to |frames_| and adjust padding.
  void AddPingFrames();

  // Randomly reorder |frames_|.
  void ReorderFrames();

  // Add PADDING frames randomly between all other frames.
  void SpreadPadding();

  // Serialize |frames_| using |framer_|.
  std::optional<size_t> BuildPacket(const QuicPacketHeader& header,
                                    char* buffer);

  size_t packet_size_;
  std::unique_ptr<char[]> crypto_frame_buffer_;
  const char* crypto_data_buffer_ = nullptr;
  QuicByteCount crypto_data_length_;
  QuicStreamOffset crypto_buffer_offset_;
  EncryptionLevel level_;
  int remaining_padding_bytes_;
  QuicFrames frames_;   // Inner frames owned, will be deleted by destructor.
  QuicFramer* framer_;  // Unowned.
  QuicRandom* random_;  // Unowned.
};

// End of old code, start of new code.

namespace test {
class QuicChaosProtectorTest;
}

// QuicChaosProtector will take a crypto frame and an amount of padding and
// build a data packet that will parse to something equivalent.
class QUICHE_EXPORT QuicChaosProtector : public QuicStreamFrameDataProducer {
 public:
  // |framer| and |random| must be valid for the lifetime of
  // QuicChaosProtector.
  explicit QuicChaosProtector(size_t packet_size, EncryptionLevel level,
                              QuicFramer* framer, QuicRandom* random);

  ~QuicChaosProtector() override;

  QuicChaosProtector(const QuicChaosProtector&) = delete;
  QuicChaosProtector(QuicChaosProtector&&) = delete;
  QuicChaosProtector& operator=(const QuicChaosProtector&) = delete;
  QuicChaosProtector& operator=(QuicChaosProtector&&) = delete;

  // Attempts to build a data packet with chaos protection. If an error occurs,
  // then std::nullopt is returned. Otherwise returns the serialized length.
  std::optional<size_t> BuildDataPacket(const QuicPacketHeader& header,
                                        const QuicFrames& frames, char* buffer);

  // From QuicStreamFrameDataProducer.
  WriteStreamDataResult WriteStreamData(QuicStreamId id,
                                        QuicStreamOffset offset,
                                        QuicByteCount data_length,
                                        QuicDataWriter* /*writer*/) override;
  bool WriteCryptoData(EncryptionLevel level, QuicStreamOffset offset,
                       QuicByteCount data_length,
                       QuicDataWriter* writer) override;

 private:
  friend class test::QuicChaosProtectorTest;

  // Ingest the frames to be chaos protected.
  bool IngestFrames(const QuicFrames& frames);

  // Allocate the crypto data buffer, create the CRYPTO frame and write the
  // crypto data to our buffer.
  bool CopyCryptoDataToLocalBuffer();

  // Split the CRYPTO frame in |frames_| into one or more CRYPTO frames that
  // collectively represent the same data. Adjusts padding to compensate.
  void SplitCryptoFrame();

  // Add a random number of PING frames to |frames_| and adjust padding.
  void AddPingFrames();

  // Randomly reorder |frames_|.
  void ReorderFrames();

  // Add PADDING frames randomly between all other frames.
  void SpreadPadding();

  // Serialize |frames_| using |framer_|.
  std::optional<size_t> BuildPacket(const QuicPacketHeader& header,
                                    char* buffer);

  const bool avoid_copy_;  // Latched from quic_chaos_protector_avoid_copy flag.
  size_t packet_size_;
  std::unique_ptr<char[]> crypto_frame_buffer_;
  const char* crypto_data_buffer_ = nullptr;
  QuicByteCount crypto_data_length_ = 0;
  QuicStreamOffset crypto_buffer_offset_ = 0;
  EncryptionLevel level_;
  int remaining_padding_bytes_ = 0;
  QuicFrames frames_;   // Inner frames owned, will be deleted by destructor.
  QuicFramer* framer_;  // Unowned.
  QuicRandom* random_;  // Unowned.
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_CHAOS_PROTECTOR_H_
