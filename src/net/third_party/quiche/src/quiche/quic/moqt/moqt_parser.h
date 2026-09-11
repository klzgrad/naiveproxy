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
#include <utility>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/quic/core/quic_data_reader.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/moqt/moqt_error.h"
#include "quiche/quic/moqt/moqt_key_value_pair.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_names.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/common/quiche_status_utils.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

namespace test {
class MoqtDataParserPeer;
}

// MoqtRawControlMessage represents an MOQT control message that has been
// unframed from the control stream, but not parsed yet.
struct MoqtRawControlMessage {
  MoqtMessageType type;
  std::string payload;
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

// MoqtStreamTypeParser reads the initial varint from a WebTransport stream to
// determine its type before constructing either a control or a data stream
// parser. Note that both of those parsers can be safely constructed from the
// stream type parser even if the parser has not read the type yet.
class QUICHE_EXPORT MoqtStreamTypeParser {
 public:
  explicit MoqtStreamTypeParser(webtransport::Stream* absl_nonnull stream)
      : stream_(stream) {}
  ~MoqtStreamTypeParser() = default;

  // Move-only semantics to avoid a stream being accessed by two different
  // parsers at the same time.
  MoqtStreamTypeParser(const MoqtStreamTypeParser&) = delete;
  MoqtStreamTypeParser& operator=(const MoqtStreamTypeParser&) = delete;
  MoqtStreamTypeParser(MoqtStreamTypeParser&& other) noexcept;
  MoqtStreamTypeParser& operator=(MoqtStreamTypeParser&& other) noexcept;

  // Reads the first varint from the stream. Returns kUnavailable if the type
  // has not been received yet.
  absl::StatusOr<uint64_t> ReadStreamType();

  std::optional<uint64_t> stream_type() const { return type_; }
  webtransport::Stream* absl_nonnull stream() const { return stream_; }

 private:
  webtransport::Stream* absl_nonnull stream_;
  std::optional<uint64_t> type_;
  absl::Status status_ = absl::OkStatus();
};

// MoqtControlStreamParser unframes MoQT control messages from the control
// stream without parsing the payload.
class QUICHE_EXPORT MoqtControlStreamParser {
 public:
  explicit MoqtControlStreamParser(webtransport::Stream* absl_nonnull stream)
      : stream_(*stream) {}
  explicit MoqtControlStreamParser(MoqtStreamTypeParser type_parser);

  // MoqtControlStreamParser is not movable, since reading from the same stream
  // through two different parsers would corrupt the state.
  MoqtControlStreamParser(const MoqtControlStreamParser&) = delete;
  MoqtControlStreamParser(MoqtControlStreamParser&& other) = delete;
  MoqtControlStreamParser& operator=(const MoqtControlStreamParser&) = delete;
  MoqtControlStreamParser& operator=(MoqtControlStreamParser&&) = delete;

  // Reads the next available message on the stream.  Returns kUnavailable
  // status if no complete message can be read; if FIN is read, `fin_read` will
  // be set to true.
  absl::StatusOr<MoqtRawControlMessage> ReadNextMessage();

  bool fin_read() const { return fin_read_; }
  webtransport::Stream* stream() const { return &stream_; }

  // Initially, MoqtControlStreamParser does not allow a control stream to have
  // a FIN. Once the type of the stream is established, that restriction can be
  // lifted.
  bool allow_fin() const { return allow_fin_; }
  void set_allow_fin(bool allow_fin) { allow_fin_ = allow_fin; }

 private:
  absl::StatusOr<MoqtRawControlMessage> ReadNextMessageInner();
  // Reads the message type from the stream.
  absl::Status ReadMessageType();

  webtransport::Stream& stream_;
  std::optional<uint64_t> current_message_type_;
  std::optional<absl::Span<char>> current_message_remaining_;
  std::string current_message_;
  bool allow_fin_ = false;
  bool error_encountered_ = false;
  bool fin_read_ = false;
};

// MoqtControlMessageParser parses MOQT control messages.  The parsing is
// stateless; the object itself only carries the context (protocol version and
// parameters) required to parse messages.
class MoqtControlMessageParser {
 public:
  // `moqt_version` is not currently used, as we only support one version.
  MoqtControlMessageParser(absl::string_view /*moqt_version*/,
                           bool uses_web_transport,
                           quic::Perspective perspective)
      : uses_web_transport_(uses_web_transport), perspective_(perspective) {}

  // Parsers for individual messages.
  absl::StatusOr<MoqtSetup> ProcessSetup(absl::string_view data) const;
  absl::StatusOr<MoqtRequestOk> ProcessRequestOk(absl::string_view data) const;
  absl::StatusOr<MoqtRequestError> ProcessRequestError(
      absl::string_view data) const;
  absl::StatusOr<MoqtSubscribe> ProcessSubscribe(absl::string_view data) const;
  absl::StatusOr<MoqtSubscribeOk> ProcessSubscribeOk(
      absl::string_view data) const;
  absl::StatusOr<MoqtPublishDone> ProcessPublishDone(
      absl::string_view data) const;
  absl::StatusOr<MoqtRequestUpdate> ProcessRequestUpdate(
      absl::string_view data) const;
  absl::StatusOr<MoqtPublishNamespace> ProcessPublishNamespace(
      absl::string_view data) const;
  absl::StatusOr<MoqtNamespace> ProcessNamespace(absl::string_view data) const;
  absl::StatusOr<MoqtNamespaceDone> ProcessNamespaceDone(
      absl::string_view data) const;
  absl::StatusOr<MoqtTrackStatus> ProcessTrackStatus(
      absl::string_view data) const;
  absl::StatusOr<MoqtGoAway> ProcessGoAway(absl::string_view data) const;
  absl::StatusOr<MoqtSubscribeNamespace> ProcessSubscribeNamespace(
      absl::string_view data) const;
  absl::StatusOr<MoqtSubscribeTracks> ProcessSubscribeTracks(
      absl::string_view data) const;
  absl::StatusOr<MoqtMaxRequestId> ProcessMaxRequestId(
      absl::string_view data) const;
  absl::StatusOr<MoqtFetch> ProcessFetch(absl::string_view data) const;
  absl::StatusOr<MoqtFetchCancel> ProcessFetchCancel(
      absl::string_view data) const;
  absl::StatusOr<MoqtFetchOk> ProcessFetchOk(absl::string_view data) const;
  absl::StatusOr<MoqtRequestsBlocked> ProcessRequestsBlocked(
      absl::string_view data) const;
  absl::StatusOr<MoqtPublish> ProcessPublish(absl::string_view data) const;
  absl::StatusOr<MoqtObjectAck> ProcessObjectAck(absl::string_view data) const;

  // Parse a raw message and call a callback on it if successful.
  // Example usage:
  //
  //     parser_.ParseMessage(message, [] (const auto& message) {
  //         QUICHE_LOG(INFO) << "Received message: " <<  message;
  //         return absl::OkStatus();
  //     });
  template <typename F>
  absl::Status ParseMessage(const MoqtRawControlMessage& message,
                            const F& callback) const {
    const auto parse = [&](auto parse_method) -> absl::Status {
      auto parsed_message = (this->*parse_method)(message.payload);
      QUICHE_RETURN_IF_ERROR(parsed_message.status());
      return callback(*std::move(parsed_message));
    };
    switch (message.type) {
      case MoqtMessageType::kSetup:
        return parse(&MoqtControlMessageParser::ProcessSetup);
      case MoqtMessageType::kRequestOk:
        return parse(&MoqtControlMessageParser::ProcessRequestOk);
      case MoqtMessageType::kRequestError:
        return parse(&MoqtControlMessageParser::ProcessRequestError);
      case MoqtMessageType::kSubscribe:
        return parse(&MoqtControlMessageParser::ProcessSubscribe);
      case MoqtMessageType::kSubscribeOk:
        return parse(&MoqtControlMessageParser::ProcessSubscribeOk);
      case MoqtMessageType::kPublishDone:
        return parse(&MoqtControlMessageParser::ProcessPublishDone);
      case MoqtMessageType::kRequestUpdate:
        return parse(&MoqtControlMessageParser::ProcessRequestUpdate);
      case MoqtMessageType::kPublishNamespace:
        return parse(&MoqtControlMessageParser::ProcessPublishNamespace);
      case MoqtMessageType::kNamespace:
        return parse(&MoqtControlMessageParser::ProcessNamespace);
      case MoqtMessageType::kNamespaceDone:
        return parse(&MoqtControlMessageParser::ProcessNamespaceDone);
      case MoqtMessageType::kTrackStatus:
        return parse(&MoqtControlMessageParser::ProcessTrackStatus);
      case MoqtMessageType::kGoAway:
        return parse(&MoqtControlMessageParser::ProcessGoAway);
      case MoqtMessageType::kSubscribeNamespace:
        return parse(&MoqtControlMessageParser::ProcessSubscribeNamespace);
      case MoqtMessageType::kSubscribeTracks:
        return parse(&MoqtControlMessageParser::ProcessSubscribeTracks);
      case MoqtMessageType::kMaxRequestId:
        return parse(&MoqtControlMessageParser::ProcessMaxRequestId);
      case MoqtMessageType::kFetch:
        return parse(&MoqtControlMessageParser::ProcessFetch);
      case MoqtMessageType::kFetchCancel:
        return parse(&MoqtControlMessageParser::ProcessFetchCancel);
      case MoqtMessageType::kFetchOk:
        return parse(&MoqtControlMessageParser::ProcessFetchOk);
      case MoqtMessageType::kRequestsBlocked:
        return parse(&MoqtControlMessageParser::ProcessRequestsBlocked);
      case MoqtMessageType::kPublish:
        return parse(&MoqtControlMessageParser::ProcessPublish);
      case MoqtMessageType::kObjectAck:
        return parse(&MoqtControlMessageParser::ProcessObjectAck);
      default:
        return absl::InvalidArgumentError(
            absl::StrCat("Unknown control message type 0x",
                         absl::Hex(static_cast<uint64_t>(message.type))));
    }
  }

 private:
  // Reads a TrackNamespace from the reader. Returns false if the namespace is
  // too large. Sets a ParseError if the namespace is malformed.
  absl::Status ReadTrackNamespace(quic::QuicDataReader& reader,
                                  TrackNamespace& track_namespace) const;
  // Reads a FullTrackName from the reader. Returns false if the name is too
  // large. Sets a ParseError if the name is malformed.
  absl::Status ReadFullTrackName(quic::QuicDataReader& reader,
                                 FullTrackName& full_track_name) const;
  absl::Status FillAndValidateSetupParameters(const KeyValuePairList& in,
                                              SetupParameters& out) const;
  // |reader| points to the beginning of a KeyValuePairList. Returns false if
  // there is any sort of error. (The function calls ParseError(), so the
  // caller has no need to do so.)
  absl::Status FillAndValidateMessageParameters(quic::QuicDataReader& reader,
                                                MessageParameters& out) const;

  bool uses_web_transport_;
  const quic::Perspective perspective_;
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
  MoqtDataParser(MoqtStreamTypeParser type_parser,
                 MoqtDataParserVisitor* visitor);

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
  std::optional<uint64_t> ReadMoqVarIntNoFin();
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
  void ProcessStreamType(uint64_t raw_type);

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
