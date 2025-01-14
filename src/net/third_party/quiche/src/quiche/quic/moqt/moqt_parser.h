// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A parser for draft-ietf-moq-transport.
// TODO(vasilvv): possibly split this header into two.

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

class QUICHE_EXPORT MoqtControlParserVisitor {
 public:
  virtual ~MoqtControlParserVisitor() = default;

  // All of these are called only when the entire message has arrived. The
  // parser retains ownership of the memory.
  virtual void OnClientSetupMessage(const MoqtClientSetup& message) = 0;
  virtual void OnServerSetupMessage(const MoqtServerSetup& message) = 0;
  virtual void OnSubscribeMessage(const MoqtSubscribe& message) = 0;
  virtual void OnSubscribeOkMessage(const MoqtSubscribeOk& message) = 0;
  virtual void OnSubscribeErrorMessage(const MoqtSubscribeError& message) = 0;
  virtual void OnUnsubscribeMessage(const MoqtUnsubscribe& message) = 0;
  virtual void OnSubscribeDoneMessage(const MoqtSubscribeDone& message) = 0;
  virtual void OnSubscribeUpdateMessage(const MoqtSubscribeUpdate& message) = 0;
  virtual void OnAnnounceMessage(const MoqtAnnounce& message) = 0;
  virtual void OnAnnounceOkMessage(const MoqtAnnounceOk& message) = 0;
  virtual void OnAnnounceErrorMessage(const MoqtAnnounceError& message) = 0;
  virtual void OnAnnounceCancelMessage(const MoqtAnnounceCancel& message) = 0;
  virtual void OnTrackStatusRequestMessage(
      const MoqtTrackStatusRequest& message) = 0;
  virtual void OnUnannounceMessage(const MoqtUnannounce& message) = 0;
  virtual void OnTrackStatusMessage(const MoqtTrackStatus& message) = 0;
  virtual void OnGoAwayMessage(const MoqtGoAway& message) = 0;
  virtual void OnSubscribeAnnouncesMessage(
      const MoqtSubscribeAnnounces& message) = 0;
  virtual void OnSubscribeAnnouncesOkMessage(
      const MoqtSubscribeAnnouncesOk& message) = 0;
  virtual void OnSubscribeAnnouncesErrorMessage(
      const MoqtSubscribeAnnouncesError& message) = 0;
  virtual void OnUnsubscribeAnnouncesMessage(
      const MoqtUnsubscribeAnnounces& message) = 0;
  virtual void OnMaxSubscribeIdMessage(const MoqtMaxSubscribeId& message) = 0;
  virtual void OnFetchMessage(const MoqtFetch& message) = 0;
  virtual void OnFetchCancelMessage(const MoqtFetchCancel& message) = 0;
  virtual void OnFetchOkMessage(const MoqtFetchOk& message) = 0;
  virtual void OnFetchErrorMessage(const MoqtFetchError& message) = 0;
  virtual void OnObjectAckMessage(const MoqtObjectAck& message) = 0;

  virtual void OnParsingError(MoqtError code, absl::string_view reason) = 0;
};

class MoqtDataParserVisitor {
 public:
  virtual ~MoqtDataParserVisitor() = default;

  // If |end_of_message| is true, |payload| contains the last bytes of the
  // OBJECT payload. If not, there will be subsequent calls with further payload
  // data. The parser retains ownership of |message| and |payload|, so the
  // visitor needs to copy anything it wants to retain.
  virtual void OnObjectMessage(const MoqtObject& message,
                               absl::string_view payload,
                               bool end_of_message) = 0;

  virtual void OnParsingError(MoqtError code, absl::string_view reason) = 0;
};

class QUICHE_EXPORT MoqtControlParser {
 public:
  MoqtControlParser(bool uses_web_transport, MoqtControlParserVisitor& visitor)
      : visitor_(visitor), uses_web_transport_(uses_web_transport) {}
  ~MoqtControlParser() = default;

  // Take a buffer from the transport in |data|. Parse each complete message and
  // call the appropriate visitor function. If |fin| is true, there
  // is no more data arriving on the stream, so the parser will deliver any
  // message encoded as to run to the end of the stream.
  // All bytes can be freed. Calls OnParsingError() when there is a parsing
  // error.
  // Any calls after sending |fin| = true will be ignored.
  // TODO(martinduke): Figure out what has to happen if the message arrives via
  // datagram rather than a stream.
  void ProcessData(absl::string_view data, bool fin);

 private:
  // The central switch statement to dispatch a message to the correct
  // Process* function. Returns 0 if it could not parse the full messsage
  // (except for object payload). Otherwise, returns the number of bytes
  // processed.
  size_t ProcessMessage(absl::string_view data);

  // The Process* functions parse the serialized data into the appropriate
  // structs, and call the relevant visitor function for further action. Returns
  // the number of bytes consumed if the message is complete; returns 0
  // otherwise.
  size_t ProcessClientSetup(quic::QuicDataReader& reader);
  size_t ProcessServerSetup(quic::QuicDataReader& reader);
  size_t ProcessSubscribe(quic::QuicDataReader& reader);
  size_t ProcessSubscribeOk(quic::QuicDataReader& reader);
  size_t ProcessSubscribeError(quic::QuicDataReader& reader);
  size_t ProcessUnsubscribe(quic::QuicDataReader& reader);
  size_t ProcessSubscribeDone(quic::QuicDataReader& reader);
  size_t ProcessSubscribeUpdate(quic::QuicDataReader& reader);
  size_t ProcessAnnounce(quic::QuicDataReader& reader);
  size_t ProcessAnnounceOk(quic::QuicDataReader& reader);
  size_t ProcessAnnounceError(quic::QuicDataReader& reader);
  size_t ProcessAnnounceCancel(quic::QuicDataReader& reader);
  size_t ProcessTrackStatusRequest(quic::QuicDataReader& reader);
  size_t ProcessUnannounce(quic::QuicDataReader& reader);
  size_t ProcessTrackStatus(quic::QuicDataReader& reader);
  size_t ProcessGoAway(quic::QuicDataReader& reader);
  size_t ProcessSubscribeAnnounces(quic::QuicDataReader& reader);
  size_t ProcessSubscribeAnnouncesOk(quic::QuicDataReader& reader);
  size_t ProcessSubscribeAnnouncesError(quic::QuicDataReader& reader);
  size_t ProcessUnsubscribeAnnounces(quic::QuicDataReader& reader);
  size_t ProcessMaxSubscribeId(quic::QuicDataReader& reader);
  size_t ProcessFetch(quic::QuicDataReader& reader);
  size_t ProcessFetchCancel(quic::QuicDataReader& reader);
  size_t ProcessFetchOk(quic::QuicDataReader& reader);
  size_t ProcessFetchError(quic::QuicDataReader& reader);
  size_t ProcessObjectAck(quic::QuicDataReader& reader);

  // If |error| is not provided, assumes kProtocolViolation.
  void ParseError(absl::string_view reason);
  void ParseError(MoqtError error, absl::string_view reason);

  // Reads an integer whose length is specified by a preceding VarInt62 and
  // returns it in |result|. Returns false if parsing fails.
  bool ReadVarIntPieceVarInt62(quic::QuicDataReader& reader, uint64_t& result);
  // Read a parameter and return the value as a string_view. Returns false if
  // |reader| does not have enough data.
  bool ReadParameter(quic::QuicDataReader& reader, uint64_t& type,
                     absl::string_view& value);
  // Reads MoqtSubscribeParameter from one of the message types that supports
  // it. The cursor in |reader| should point to the "number of parameters"
  // field in the message. The cursor will move to the end of the parameters.
  // Returns false if it could not parse the full message, in which case the
  // cursor in |reader| should not be used.
  bool ReadSubscribeParameters(quic::QuicDataReader& reader,
                               MoqtSubscribeParameters& params);
  // Convert a string view to a varint. Throws an error and returns false if the
  // string_view is not exactly the right length.
  bool StringViewToVarInt(absl::string_view& sv, uint64_t& vi);

  // Parses a message that a track namespace but not name. The last element of
  // |full_track_name| will be set to the empty string. Returns false if it
  // could not parse the full namespace field.
  bool ReadTrackNamespace(quic::QuicDataReader& reader,
                          FullTrackName& full_track_name);

  MoqtControlParserVisitor& visitor_;
  bool uses_web_transport_;
  bool no_more_data_ = false;  // Fatal error or fin. No more parsing.
  bool parsing_error_ = false;

  std::string buffered_message_;

  bool processing_ = false;  // True if currently in ProcessData(), to prevent
                             // re-entrancy.
};

// Parses an MoQT datagram. Returns the payload bytes, or empty string_view on
// error. The caller provides the whole datagram in `data`.  The function puts
// the object metadata in `object_metadata`.
absl::string_view ParseDatagram(absl::string_view data,
                                MoqtObject& object_metadata);

// Parser for MoQT unidirectional data stream.
class QUICHE_EXPORT MoqtDataParser {
 public:
  explicit MoqtDataParser(MoqtDataParserVisitor* visitor)
      : visitor_(*visitor) {}
  ~MoqtDataParser() = default;

  // Take a buffer from the transport in |data|. Parse each complete message and
  // call the appropriate visitor function. If |fin| is true, there
  // is no more data arriving on the stream, so the parser will deliver any
  // message encoded as to run to the end of the stream.
  // All bytes can be freed. Calls OnParsingError() when there is a parsing
  // error.
  void ProcessData(absl::string_view data, bool fin);

  // Alters `chunk_size_` value (see discussion below).  Primarily intended to
  // be used for testing.
  void set_chunk_size(size_t size) { chunk_size_ = size; }

 private:
  // If there is buffered data from the previous attempt at parsing it, new data
  // will be added in `chunk_size_`-sized chunks.
  constexpr static size_t kDefaultChunkSize = 64;

  // Current state of the parser.
  enum NextInput {
    // Nothing has been read yet; the next thing to be read is the stream type
    // varint.
    kStreamType,
    // The next thing to be read is the stream header.
    kHeader,
    // The next thing to be read is the stream subheader for the given object.
    kSubheader,
    // The next thing to be read is the object payload.
    kData,
    // The next thing to be read (and ignored) is padding.
    kPadding,
  };

  // Infers the current state of the parser.
  NextInput GetNextInput() const {
    if (!type_.has_value()) {
      return kStreamType;
    }
    if (type_ == MoqtDataStreamType::kPadding) {
      return kPadding;
    }
    if (!metadata_.has_value()) {
      return kHeader;
    }
    if (payload_length_remaining_ > 0) {
      return kData;
    }
    return kSubheader;
  }

  // Processes all that can be entirely processed, and returns the view for the
  // data that needs to be buffered.
  absl::string_view ProcessDataInner(absl::string_view data);

  void ParseError(absl::string_view reason);

  MoqtDataParserVisitor& visitor_;
  size_t chunk_size_ = kDefaultChunkSize;

  bool no_more_data_ = false;  // Fatal error or fin. No more parsing.
  bool parsing_error_ = false;

  std::string buffered_message_;

  // The three variables below implicitly drive the state machine; see
  // `GetNextInput()` for how the state is derived.
  std::optional<MoqtDataStreamType> type_ = std::nullopt;
  std::optional<MoqtObject> metadata_ = std::nullopt;
  size_t payload_length_remaining_ = 0;

  bool processing_ = false;  // True if currently in ProcessData(), to prevent
                             // re-entrancy.
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_PARSER_H_
