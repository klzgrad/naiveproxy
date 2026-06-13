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

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_data_reader.h"
#include "quiche/quic/moqt/moqt_error.h"
#include "quiche/quic/moqt/moqt_key_value_pair.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_names.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/web_transport/web_transport.h"

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
  virtual void OnRequestOkMessage(const MoqtRequestOk& message) = 0;
  virtual void OnRequestErrorMessage(const MoqtRequestError& message) = 0;
  virtual void OnSubscribeMessage(const MoqtSubscribe& message) = 0;
  virtual void OnSubscribeOkMessage(const MoqtSubscribeOk& message) = 0;
  virtual void OnUnsubscribeMessage(const MoqtUnsubscribe& message) = 0;
  virtual void OnPublishDoneMessage(const MoqtPublishDone& message) = 0;
  virtual void OnRequestUpdateMessage(const MoqtRequestUpdate& message) = 0;
  virtual void OnPublishNamespaceMessage(
      const MoqtPublishNamespace& message) = 0;
  virtual void OnPublishNamespaceDoneMessage(
      const MoqtPublishNamespaceDone& message) = 0;
  virtual void OnNamespaceMessage(const MoqtNamespace& message) = 0;
  virtual void OnNamespaceDoneMessage(const MoqtNamespaceDone& message) = 0;
  virtual void OnPublishNamespaceCancelMessage(
      const MoqtPublishNamespaceCancel& message) = 0;
  virtual void OnTrackStatusMessage(const MoqtTrackStatus& message) = 0;
  virtual void OnGoAwayMessage(const MoqtGoAway& message) = 0;
  virtual void OnSubscribeNamespaceMessage(
      const MoqtSubscribeNamespace& message) = 0;
  virtual void OnMaxRequestIdMessage(const MoqtMaxRequestId& message) = 0;
  virtual void OnFetchMessage(const MoqtFetch& message) = 0;
  virtual void OnFetchCancelMessage(const MoqtFetchCancel& message) = 0;
  virtual void OnFetchOkMessage(const MoqtFetchOk& message) = 0;
  virtual void OnRequestsBlockedMessage(const MoqtRequestsBlocked& message) = 0;
  virtual void OnPublishMessage(const MoqtPublish& message) = 0;
  virtual void OnPublishOkMessage(const MoqtPublishOk& message) = 0;
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
  // If `message.object_status` == `kNormal`, the status must not be used until
  // `end_of_message` is true, since a FIN can change the status.
  virtual void OnObjectMessage(const MoqtObject& message,
                               absl::string_view payload,
                               bool end_of_message) = 0;
  virtual void OnFin() = 0;

  virtual void OnParsingError(MoqtError code, absl::string_view reason) = 0;
};

class QUICHE_EXPORT MoqtMessageTypeParser {
 public:
  MoqtMessageTypeParser(webtransport::Stream* stream) : stream_(*stream) {}
  ~MoqtMessageTypeParser() = default;

  // Returns false if there was a FIN.
  bool ReadUntilMessageTypeKnown();
  std::optional<uint64_t> message_type() const { return message_type_; }

 private:
  webtransport::Stream& stream_;
  std::optional<uint64_t> message_type_;
};

class QUICHE_EXPORT MoqtControlParser {
 public:
  MoqtControlParser(bool uses_web_transport, webtransport::Stream* stream,
                    MoqtControlParserVisitor& visitor)
      : visitor_(visitor),
        stream_(*stream),
        uses_web_transport_(uses_web_transport) {}
  ~MoqtControlParser() = default;

  void set_message_type(uint64_t message_type) { message_type_ = message_type; }
  void ReadAndDispatchMessages();

 private:
  // The central switch statement to dispatch a message to the correct
  // Process* function. Invokles an error callback if parsing fails.
  void ProcessMessage(absl::string_view data, MoqtMessageType message_type);

  // The Process* functions parse the serialized data into the appropriate
  // structs, and call the relevant visitor function for further action. Returns
  // the number of bytes consumed if the message is complete; returns 0
  // otherwise.
  absl::Status ProcessClientSetup(absl::string_view data);
  absl::Status ProcessServerSetup(absl::string_view data);
  absl::Status ProcessRequestOk(absl::string_view data);
  absl::Status ProcessRequestError(absl::string_view data);
  // Subscribe formats are used for TrackStatus as well, so take the message
  // type as an argument, defaulting to the subscribe version.
  absl::Status ProcessSubscribe(
      absl::string_view data,
      MoqtMessageType message_type = MoqtMessageType::kSubscribe);
  absl::Status ProcessSubscribeOk(absl::string_view data);
  absl::Status ProcessUnsubscribe(absl::string_view data);
  absl::Status ProcessPublishDone(absl::string_view data);
  absl::Status ProcessRequestUpdate(absl::string_view data);
  absl::Status ProcessPublishNamespace(absl::string_view data);
  absl::Status ProcessPublishNamespaceDone(absl::string_view data);
  absl::Status ProcessNamespace(absl::string_view data);
  absl::Status ProcessNamespaceDone(absl::string_view data);
  absl::Status ProcessPublishNamespaceCancel(absl::string_view data);
  absl::Status ProcessTrackStatus(absl::string_view data);
  absl::Status ProcessGoAway(absl::string_view data);
  absl::Status ProcessSubscribeNamespace(absl::string_view data);
  absl::Status ProcessMaxRequestId(absl::string_view data);
  absl::Status ProcessFetch(absl::string_view data);
  absl::Status ProcessFetchCancel(absl::string_view data);
  absl::Status ProcessFetchOk(absl::string_view data);
  absl::Status ProcessRequestsBlocked(absl::string_view data);
  absl::Status ProcessPublish(absl::string_view data);
  absl::Status ProcessPublishOk(absl::string_view data);
  absl::Status ProcessObjectAck(absl::string_view data);

  // If |error| is not provided, assumes kProtocolViolation.
  void ParseError(absl::string_view reason);
  void ParseError(const absl::Status& status);
  void ParseError(MoqtError error, absl::string_view reason);

  // Reads a TrackNamespace from the reader. Returns false if the namespace is
  // too large. Sets a ParseError if the namespace is malformed.
  absl::Status ReadTrackNamespace(quic::QuicDataReader& reader,
                                  TrackNamespace& track_namespace);
  // Reads a FullTrackName from the reader. Returns false if the name is too
  // large. Sets a ParseError if the name is malformed.
  absl::Status ReadFullTrackName(quic::QuicDataReader& reader,
                                 FullTrackName& full_track_name);
  absl::Status FillAndValidateSetupParameters(const KeyValuePairList& in,
                                              SetupParameters& out,
                                              MoqtMessageType message_type);
  // |reader| points to the beginning of a KeyValuePairList. Returns false if
  // there is any sort of error. (The function calls ParseError(), so the
  // caller has no need to do so.)
  absl::Status FillAndValidateMessageParameters(quic::QuicDataReader& reader,
                                                MessageParameters& out);

  MoqtControlParserVisitor& visitor_;
  webtransport::Stream& stream_;
  bool uses_web_transport_;
  bool no_more_data_ = false;  // Fatal error or fin. No more parsing.
  bool parsing_error_ = false;

  std::optional<uint64_t> message_type_;
  std::optional<uint16_t> message_size_;

  uint64_t max_auth_token_cache_size_ = 0;
  uint64_t auth_token_cache_size_ = 0;
  bool processing_ = false;  // True if currently in ProcessData(), to prevent
                             // re-entrancy.
};

// Parses an MoQT datagram. Returns the payload bytes, or std::nullopt on error.
// The caller provides the whole datagram in `data`.  The function puts the
// object metadata in `object_metadata`.
// If |use_default_priority| returns true, there was no reported
// publisher_priority and the caller should use the default for the SUBSCRIBE.
std::optional<absl::string_view> ParseDatagram(absl::string_view data,
                                               MoqtObject& object_metadata,
                                               bool& use_default_priority);

// Parser for MoQT unidirectional data stream.
class QUICHE_EXPORT MoqtDataParser {
 public:
  // `stream` must outlive the parser.  The parser does not configure itself as
  // a listener for the read events of the stream; it is responsibility of the
  // caller to do so via one of the read methods below.
  explicit MoqtDataParser(webtransport::Stream* stream,
                          MoqtDataParserVisitor* visitor)
      : stream_(*stream), visitor_(*visitor) {}

  // Reads all of the available objects on the stream.
  void ReadAllData();

  void ReadStreamType();
  void ReadTrackAlias();
  void ReadAtMostOneObject();

  // Returns the type of the unidirectional stream, if already known.
  std::optional<MoqtDataStreamType> stream_type() const {
    if (next_input_ == kStreamType) {
      return std::nullopt;
    }
    return type_;
  }

  // Returns the track alias, if already known.
  std::optional<uint64_t> track_alias() const {
    return (next_input_ == kStreamType || next_input_ == kTrackAlias ||
            next_input_ == kRequestId)
               ? std::optional<uint64_t>()
               : metadata_.track_alias;
  }

  void set_default_publisher_priority(MoqtPriority priority) {
    default_publisher_priority_ = priority;
  }

 private:
  friend class test::MoqtDataParserPeer;

  // Current state of the parser.
  enum NextInput {
    kStreamType,
    kTrackAlias,          // SUBSCRIBE/PUBLISH only.
    kRequestId,           // FETCH only.
    kSerializationFlags,  // FETCH only.
    kGroupId,
    kSubgroupId,
    kPublisherPriority,
    kObjectId,
    kExtensionSize,
    kExtensionBody,
    kObjectPayloadLength,
    kStatus,
    kData,
    kAwaitingNextByte,  // Can't determine status until the next byte arrives.
    kPadding,
    kFailed,
  };

  // If a StopCondition callback returns true, parsing will terminate.
  using StopCondition = quiche::UnretainedCallback<bool()>;

  struct State {
    NextInput next_input;
    uint64_t payload_remaining;

    bool operator==(const State&) const = default;
  };
  State state() const { return State{next_input_, payload_length_remaining_}; }

  void ReadDataUntil(StopCondition stop_condition);

  // Reads a single varint from the underlying stream. Triggers a parse error if
  // a FIN has been encountered.
  std::optional<uint64_t> ReadVarInt62NoFin();
  // Reads a single uint8 from the underlying stream. Triggers a parse error if
  // a FIN has been encountered.
  std::optional<uint8_t> ReadUint8NoFin();

  // Advances the state machine of the parser to the next expected state.
  [[nodiscard]] NextInput AdvanceParserState();
  // Reads the next available item from the stream.
  void ParseNextItemFromStream();
  // Checks if we have encountered a FIN without data.  If so, processes it and
  // returns true.
  bool CheckForFinWithoutData();

  void ParseError(absl::string_view reason);

  webtransport::Stream& stream_;
  MoqtDataParserVisitor& visitor_;

  bool no_more_data_ = false;  // Fatal error or fin. No more parsing.
  bool parsing_error_ = false;
  bool contains_end_of_group_ = false;  // True if the stream contains an
                                        // implied END_OF_GROUP object.
  MoqtPriority default_publisher_priority_;

  std::string buffered_message_;

  MoqtDataStreamType type_;
  MoqtFetchSerialization fetch_serialization_;
  NextInput next_input_ = kStreamType;
  MoqtObject metadata_;
  std::optional<uint64_t> last_object_id_;
  size_t payload_length_remaining_ = 0;
  size_t num_objects_read_ = 0;

  bool processing_ = false;  // True if currently in ProcessData(), to prevent
                             // re-entrancy.
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_PARSER_H_
