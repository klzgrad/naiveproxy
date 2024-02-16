// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A parser for draft-ietf-moq-transport-01.

#ifndef QUICHE_QUIC_MOQT_MOQT_PARSER_H_
#define QUICHE_QUIC_MOQT_MOQT_PARSER_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_data_reader.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace moqt {

class QUICHE_EXPORT MoqtParserVisitor {
 public:
  virtual ~MoqtParserVisitor() = default;

  // If |end_of_message| is true, |payload| contains the last bytes of the
  // OBJECT payload. If not, there will be subsequent calls with further payload
  // data. The parser retains ownership of |message| and |payload|, so the
  // visitor needs to copy anything it wants to retain.
  virtual void OnObjectMessage(const MoqtObject& message,
                               absl::string_view payload,
                               bool end_of_message) = 0;
  // All of these are called only when the entire message has arrived. The
  // parser retains ownership of the memory.
  virtual void OnClientSetupMessage(const MoqtClientSetup& message) = 0;
  virtual void OnServerSetupMessage(const MoqtServerSetup& message) = 0;
  virtual void OnSubscribeRequestMessage(
      const MoqtSubscribeRequest& message) = 0;
  virtual void OnSubscribeOkMessage(const MoqtSubscribeOk& message) = 0;
  virtual void OnSubscribeErrorMessage(const MoqtSubscribeError& message) = 0;
  virtual void OnUnsubscribeMessage(const MoqtUnsubscribe& message) = 0;
  virtual void OnSubscribeFinMessage(const MoqtSubscribeFin& message) = 0;
  virtual void OnSubscribeRstMessage(const MoqtSubscribeRst& message) = 0;
  virtual void OnAnnounceMessage(const MoqtAnnounce& message) = 0;
  virtual void OnAnnounceOkMessage(const MoqtAnnounceOk& message) = 0;
  virtual void OnAnnounceErrorMessage(const MoqtAnnounceError& message) = 0;
  virtual void OnUnannounceMessage(const MoqtUnannounce& message) = 0;
  virtual void OnGoAwayMessage(const MoqtGoAway& message) = 0;

  virtual void OnParsingError(absl::string_view reason) = 0;
};

class QUICHE_EXPORT MoqtParser {
 public:
  MoqtParser(bool uses_web_transport, MoqtParserVisitor& visitor)
      : visitor_(visitor), uses_web_transport_(uses_web_transport) {}
  ~MoqtParser() = default;

  // Take a buffer from the transport in |data|. Parse each complete message and
  // call the appropriate visitor function. If |fin| is true, there
  // is no more data arriving on the stream, so the parser will deliver any
  // message encoded as to run to the end of the stream.
  // All bytes can be freed. Calls OnParsingError() when there is a parsing
  // error.
  // Any calls after sending |fin| = true will be ignored.
  void ProcessData(absl::string_view data, bool fin);

 private:
  // The central switch statement to dispatch a message to the correct
  // Process* function. Returns 0 if it could not parse the full messsage
  // (except for object payload). Otherwise, returns the number of bytes
  // processed.
  size_t ProcessMessage(absl::string_view data, bool fin);
  // The Process* functions parse the serialized data into the appropriate
  // structs, and call the relevant visitor function for further action. Returns
  // the number of bytes consumed if the message is complete; returns 0
  // otherwise.
  size_t ProcessObject(quic::QuicDataReader& reader, bool has_length, bool fin);
  size_t ProcessClientSetup(quic::QuicDataReader& reader);
  size_t ProcessServerSetup(quic::QuicDataReader& reader);
  size_t ProcessSubscribeRequest(quic::QuicDataReader& reader);
  size_t ProcessSubscribeOk(quic::QuicDataReader& reader);
  size_t ProcessSubscribeError(quic::QuicDataReader& reader);
  size_t ProcessUnsubscribe(quic::QuicDataReader& reader);
  size_t ProcessSubscribeFin(quic::QuicDataReader& reader);
  size_t ProcessSubscribeRst(quic::QuicDataReader& reader);
  size_t ProcessAnnounce(quic::QuicDataReader& reader);
  size_t ProcessAnnounceOk(quic::QuicDataReader& reader);
  size_t ProcessAnnounceError(quic::QuicDataReader& reader);
  size_t ProcessUnannounce(quic::QuicDataReader& reader);
  size_t ProcessGoAway(quic::QuicDataReader& reader);

  void ParseError(absl::string_view reason);

  // Reads an integer whose length is specified by a preceding VarInt62 and
  // returns it in |result|. Returns false if parsing fails.
  bool ReadVarIntPieceVarInt62(quic::QuicDataReader& reader, uint64_t& result);
  // Read a Location field from SUBSCRIBE REQUEST
  bool ReadLocation(quic::QuicDataReader& reader,
                    std::optional<MoqtSubscribeLocation>& loc);
  // Read a parameter and return the value as a string_view. Returns false if
  // |reader| does not have enough data.
  bool ReadParameter(quic::QuicDataReader& reader, uint64_t& type,
                     absl::string_view& value);
  // Convert a string view to a varint. Throws an error and returns false if the
  // string_view is not exactly the right length.
  bool StringViewToVarInt(absl::string_view& sv, uint64_t& vi);

  MoqtParserVisitor& visitor_;
  bool uses_web_transport_;
  bool no_more_data_ = false;  // Fatal error or fin. No more parsing.
  bool parsing_error_ = false;

  std::string buffered_message_;

  // Metadata for an object which is delivered in parts.
  std::optional<MoqtObject> object_metadata_ = std::nullopt;
  size_t payload_length_remaining_;

  bool processing_ = false;  // True if currently in ProcessData(), to prevent
                             // re-entrancy.
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_PARSER_H_
