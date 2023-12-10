// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A parser for draft-ietf-moq-transport-01.

#ifndef QUICHE_QUIC_MOQT_MOQT_PARSER_H_
#define QUICHE_QUIC_MOQT_MOQT_PARSER_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "quiche/quic/core/quic_data_reader.h"
#include "quiche/quic/core/quic_types.h"
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
  // All of these are called only when the entire specified message length has
  // arrived, which requires a stream FIN if the length is zero. The parser
  // retains ownership of the memory.
  virtual void OnSetupMessage(const MoqtSetup& message) = 0;
  virtual void OnSubscribeRequestMessage(
      const MoqtSubscribeRequest& message) = 0;
  virtual void OnSubscribeOkMessage(const MoqtSubscribeOk& message) = 0;
  virtual void OnSubscribeErrorMessage(const MoqtSubscribeError& message) = 0;
  virtual void OnUnsubscribeMessage(const MoqtUnsubscribe& message) = 0;
  virtual void OnAnnounceMessage(const MoqtAnnounce& message) = 0;
  virtual void OnAnnounceOkMessage(const MoqtAnnounceOk& message) = 0;
  virtual void OnAnnounceErrorMessage(const MoqtAnnounceError& message) = 0;
  virtual void OnUnannounceMessage(const MoqtUnannounce& message) = 0;
  // In an exception to the above, the parser calls this when it gets two bytes,
  // whether or not it includes stream FIN. When a zero-length message has
  // special meaning, a message with an actual length of zero is tricky!
  virtual void OnGoAwayMessage() = 0;

  virtual void OnParsingError(absl::string_view reason) = 0;
};

class QUICHE_EXPORT MoqtParser {
 public:
  MoqtParser(quic::Perspective perspective, bool uses_web_transport,
             MoqtParserVisitor& visitor)
      : visitor_(visitor),
        perspective_(perspective),
        uses_web_transport_(uses_web_transport) {}
  ~MoqtParser() = default;

  // Take a buffer from the transport in |data|. Parse each complete message and
  // call the appropriate visitor function. If |end_of_stream| is true, there
  // is no more data arriving on the stream, so the parser will deliver any
  // message encoded as to run to the end of the stream.
  // All bytes can be freed. Calls OnParsingError() when there is a parsing
  // error.
  // Any calls after sending |end_of_stream| = true will be ignored.
  void ProcessData(absl::string_view data, bool end_of_stream);

 private:
  // Copies the minimum amount of data in |reader| to buffered_message_ in order
  // to process what is in there, and does the processing. Returns true if
  // additional processing can occur, false otherwise.
  bool MaybeMergeDataWithBuffer(quic::QuicDataReader& reader,
                                bool end_of_stream);

  // The central switch statement to dispatch a message to the correct
  // Process* function. Returns nullopt if it could not parse the full messsage
  // (except for object payload). Otherwise, returns the number of bytes
  // processed.
  absl::optional<size_t> ProcessMessage(absl::string_view data);
  // A helper function to parse just the varints in an OBJECT.
  absl::optional<size_t> ProcessObjectVarints(absl::string_view data);
  // The Process* functions parse the serialized data into the appropriate
  // structs, and call the relevant visitor function for further action. Returns
  // the number of bytes consumed if the message is complete; returns nullopt
  // otherwise. These functions can throw a fatal error if the message length
  // is insufficient.
  absl::optional<size_t> ProcessObject(absl::string_view data);
  absl::optional<size_t> ProcessSetup(absl::string_view data);
  absl::optional<size_t> ProcessSubscribeRequest(absl::string_view data);
  absl::optional<size_t> ProcessSubscribeOk(absl::string_view data);
  absl::optional<size_t> ProcessSubscribeError(absl::string_view data);
  absl::optional<size_t> ProcessUnsubscribe(absl::string_view data);
  absl::optional<size_t> ProcessAnnounce(absl::string_view data);
  absl::optional<size_t> ProcessAnnounceOk(absl::string_view data);
  absl::optional<size_t> ProcessAnnounceError(absl::string_view data);
  absl::optional<size_t> ProcessUnannounce(absl::string_view data);
  absl::optional<size_t> ProcessGoAway(absl::string_view data);

  // If the message length field is zero, it runs to the end of the stream.
  bool NoMessageLength() { return *message_length_ == 0; }
  // If type and or length are not already stored for this message, reads it out
  // of the data in |reader| and stores it in the appropriate members. Returns
  // false if length is not available.
  bool GetMessageTypeAndLength(quic::QuicDataReader& reader);
  void EndOfMessage();
  // Get a string_view of the part of the reader covered by message_length_,
  // with exceptions for OBJECT messages.
  absl::string_view FetchMessage(quic::QuicDataReader& reader);
  void ParseError(absl::string_view reason);

  // Reads an integer whose length is specified by a preceding VarInt62 and
  // returns it in |result|. Returns false if parsing fails.
  bool ReadIntegerPieceVarInt62(quic::QuicDataReader& reader, uint64_t& result);

  MoqtParserVisitor& visitor_;
  // Client or server?
  quic::Perspective perspective_;
  bool uses_web_transport_;
  bool no_more_data_ = false;  // Fatal error or end_of_stream. No more parsing.
  bool parsing_error_ = false;

  std::string buffered_message_;
  absl::optional<MoqtMessageType> message_type_ = absl::nullopt;
  absl::optional<size_t> message_length_ = absl::nullopt;

  // Metadata for an object which is delivered in parts.
  absl::optional<MoqtObject> object_metadata_ = absl::nullopt;

  bool processing_ = false;  // True if currently in ProcessData(), to prevent
                             // re-entrancy.
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_PARSER_H_
