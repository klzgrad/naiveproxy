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
#include "quiche/common/quiche_callbacks.h"
#include "quiche/common/quiche_stream.h"

namespace moqt {

namespace test {
class MoqtDataParserPeer;
}

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
  virtual void OnSubscribesBlockedMessage(
      const MoqtSubscribesBlocked& message) = 0;
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
  size_t ProcessSubscribesBlocked(quic::QuicDataReader& reader);
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

// Parses an MoQT datagram. Returns the payload bytes, or std::nullopt on error.
// The caller provides the whole datagram in `data`.  The function puts the
// object metadata in `object_metadata`.
std::optional<absl::string_view> ParseDatagram(absl::string_view data,
                                               MoqtObject& object_metadata);

// Parser for MoQT unidirectional data stream.
class QUICHE_EXPORT MoqtDataParser {
 public:
  // `stream` must outlive the parser.  The parser does not configure itself as
  // a listener for the read events of the stream; it is responsibility of the
  // caller to do so via one of the read methods below.
  explicit MoqtDataParser(quiche::ReadStream* stream,
                          MoqtDataParserVisitor* visitor)
      : stream_(*stream), visitor_(*visitor) {}

  // Reads all of the available objects on the stream.
  void ReadAllData();

  void ReadStreamType();
  void ReadTrackAlias();
  void ReadAtMostOneObject();

  // Returns the type of the unidirectional stream, if already known.
  std::optional<MoqtDataStreamType> stream_type() const { return type_; }

  // Returns the track alias, if already known.
  std::optional<uint64_t> track_alias() const {
    return (next_input_ == kStreamType || next_input_ == kTrackAlias)
               ? std::optional<uint64_t>()
               : metadata_.track_alias;
  }

 private:
  friend class test::MoqtDataParserPeer;

  // Current state of the parser.
  enum NextInput {
    kStreamType,
    kTrackAlias,
    kGroupId,
    kSubgroupId,
    kPublisherPriority,
    kObjectId,
    kExtensionCount,
    kExtensionType,
    kExtensionVarint,
    kExtensionLength,
    kExtensionPayload,
    kObjectPayloadLength,
    kStatus,
    kData,
    kPadding,
    kFailed,
  };

  // If a StopCondition callback returns true, parsing will terminate.
  using StopCondition = quiche::UnretainedCallback<bool()>;

  struct State {
    NextInput next_input;
    uint64_t payload_remaining;
    uint64_t extensions_remaining;

    bool operator==(const State&) const = default;
  };
  State state() const {
    return State{next_input_, payload_length_remaining_, extensions_remaining_};
  }

  void ReadDataUntil(StopCondition stop_condition);

  // Reads a single varint from the underlying stream.
  std::optional<uint64_t> ReadVarInt62(bool& fin_read);
  // Reads a single varint from the underlying stream. Triggers a parse error if
  // a FIN has been encountered.
  std::optional<uint64_t> ReadVarInt62NoFin();
  // Reads a single uint8 from the underlying stream. Triggers a parse error if
  // a FIN has been encountered.
  std::optional<uint8_t> ReadUint8NoFin();

  // Advances the state machine of the parser to the next expected state.
  void AdvanceParserState();
  // Reads the next available item from the stream.
  void ParseNextItemFromStream();
  // Checks if we have encountered a FIN without data.  If so, processes it and
  // returns true.
  bool CheckForFinWithoutData();

  void ParseError(absl::string_view reason);

  quiche::ReadStream& stream_;
  MoqtDataParserVisitor& visitor_;

  bool no_more_data_ = false;  // Fatal error or fin. No more parsing.
  bool parsing_error_ = false;

  std::string buffered_message_;

  std::optional<MoqtDataStreamType> type_ = std::nullopt;
  NextInput next_input_ = kStreamType;
  MoqtObject metadata_;
  size_t payload_length_remaining_ = 0;
  uint64_t extensions_remaining_ = 0;
  size_t num_objects_read_ = 0;
  MoqtExtensionHeader current_extension_;

  bool processing_ = false;  // True if currently in ProcessData(), to prevent
                             // re-entrancy.
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_PARSER_H_
