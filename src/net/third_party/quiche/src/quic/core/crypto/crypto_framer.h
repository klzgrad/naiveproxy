// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_CRYPTO_FRAMER_H_
#define QUICHE_QUIC_CORE_CRYPTO_CRYPTO_FRAMER_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "net/third_party/quiche/src/quic/core/crypto/crypto_handshake_message.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_message_parser.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

class CryptoFramer;
class QuicData;
class QuicDataWriter;

class QUIC_EXPORT_PRIVATE CryptoFramerVisitorInterface {
 public:
  virtual ~CryptoFramerVisitorInterface() {}

  // Called if an error is detected.
  virtual void OnError(CryptoFramer* framer) = 0;

  // Called when a complete handshake message has been parsed.
  virtual void OnHandshakeMessage(const CryptoHandshakeMessage& message) = 0;
};

// A class for framing the crypto messages that are exchanged in a QUIC
// session.
class QUIC_EXPORT_PRIVATE CryptoFramer : public CryptoMessageParser {
 public:
  CryptoFramer();

  ~CryptoFramer() override;

  // ParseMessage parses exactly one message from the given
  // quiche::QuicheStringPiece. If there is an error, the message is truncated,
  // or the message has trailing garbage then nullptr will be returned.
  static std::unique_ptr<CryptoHandshakeMessage> ParseMessage(
      quiche::QuicheStringPiece in);

  // Set callbacks to be called from the framer.  A visitor must be set, or
  // else the framer will crash.  It is acceptable for the visitor to do
  // nothing.  If this is called multiple times, only the last visitor
  // will be used.  |visitor| will be owned by the framer.
  void set_visitor(CryptoFramerVisitorInterface* visitor) {
    visitor_ = visitor;
  }

  QuicErrorCode error() const override;
  const std::string& error_detail() const override;

  // Processes input data, which must be delivered in order. Returns
  // false if there was an error, and true otherwise. ProcessInput optionally
  // takes an EncryptionLevel, but it is ignored. The variant with the
  // EncryptionLevel is provided to match the CryptoMessageParser interface.
  bool ProcessInput(quiche::QuicheStringPiece input,
                    EncryptionLevel level) override;
  bool ProcessInput(quiche::QuicheStringPiece input);

  // Returns the number of bytes of buffered input data remaining to be
  // parsed.
  size_t InputBytesRemaining() const override;

  // Checks if the specified tag has been seen. Returns |true| if it
  // has, and |false| if it has not or a CHLO has not been seen.
  bool HasTag(QuicTag tag) const;

  // Even if the CHLO has not been fully received, force processing of
  // the handshake message. This is dangerous and should not be used
  // except as a mechanism of last resort.
  void ForceHandshake();

  // Returns a new QuicData owned by the caller that contains a serialized
  // |message|, or nullptr if there was an error.
  static std::unique_ptr<QuicData> ConstructHandshakeMessage(
      const CryptoHandshakeMessage& message);

  // Debug only method which permits processing truncated messages.
  void set_process_truncated_messages(bool process_truncated_messages) {
    process_truncated_messages_ = process_truncated_messages;
  }

 private:
  // Clears per-message state.  Does not clear the visitor.
  void Clear();

  // Process does does the work of |ProcessInput|, but returns an error code,
  // doesn't set error_ and doesn't call |visitor_->OnError()|.
  QuicErrorCode Process(quiche::QuicheStringPiece input);

  static bool WritePadTag(QuicDataWriter* writer,
                          size_t pad_length,
                          uint32_t* end_offset);

  // Represents the current state of the parsing state machine.
  enum CryptoFramerState {
    STATE_READING_TAG,
    STATE_READING_NUM_ENTRIES,
    STATE_READING_TAGS_AND_LENGTHS,
    STATE_READING_VALUES
  };

  // Visitor to invoke when messages are parsed.
  CryptoFramerVisitorInterface* visitor_;
  // Last error.
  QuicErrorCode error_;
  // Remaining unparsed data.
  std::string buffer_;
  // Current state of the parsing.
  CryptoFramerState state_;
  // The message currently being parsed.
  CryptoHandshakeMessage message_;
  // The issue which caused |error_|
  std::string error_detail_;
  // Number of entires in the message currently being parsed.
  uint16_t num_entries_;
  // tags_and_lengths_ contains the tags that are currently being parsed and
  // their lengths.
  std::vector<std::pair<QuicTag, size_t>> tags_and_lengths_;
  // Cumulative length of all values in the message currently being parsed.
  size_t values_len_;
  // Set to true to allow of processing of truncated messages for debugging.
  bool process_truncated_messages_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_CRYPTO_FRAMER_H_
