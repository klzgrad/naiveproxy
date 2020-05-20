// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_types.h"

#include <cstdint>

#include "third_party/boringssl/src/include/openssl/ssl.h"
#include "net/third_party/quiche/src/quic/core/quic_error_codes.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_str_cat.h"

namespace quic {

std::ostream& operator<<(std::ostream& os, const QuicConsumedData& s) {
  os << "bytes_consumed: " << s.bytes_consumed
     << " fin_consumed: " << s.fin_consumed;
  return os;
}

std::ostream& operator<<(std::ostream& os, const Perspective& s) {
  if (s == Perspective::IS_SERVER) {
    os << "IS_SERVER";
  } else {
    os << "IS_CLIENT";
  }
  return os;
}

std::ostream& operator<<(std::ostream& os, const AckedPacket& acked_packet) {
  os << "{ packet_number: " << acked_packet.packet_number
     << ", bytes_acked: " << acked_packet.bytes_acked << ", receive_timestamp: "
     << acked_packet.receive_timestamp.ToDebuggingValue() << "} ";
  return os;
}

std::ostream& operator<<(std::ostream& os, const LostPacket& lost_packet) {
  os << "{ packet_number: " << lost_packet.packet_number
     << ", bytes_lost: " << lost_packet.bytes_lost << "} ";
  return os;
}

std::string HistogramEnumString(WriteStatus enum_value) {
  switch (enum_value) {
    case WRITE_STATUS_OK:
      return "OK";
    case WRITE_STATUS_BLOCKED:
      return "BLOCKED";
    case WRITE_STATUS_BLOCKED_DATA_BUFFERED:
      return "BLOCKED_DATA_BUFFERED";
    case WRITE_STATUS_ERROR:
      return "ERROR";
    case WRITE_STATUS_MSG_TOO_BIG:
      return "MSG_TOO_BIG";
    case WRITE_STATUS_FAILED_TO_COALESCE_PACKET:
      return "WRITE_STATUS_FAILED_TO_COALESCE_PACKET";
    case WRITE_STATUS_NUM_VALUES:
      return "NUM_VALUES";
  }
  QUIC_DLOG(ERROR) << "Invalid WriteStatus value: "
                   << static_cast<int16_t>(enum_value);
  return "<invalid>";
}

std::ostream& operator<<(std::ostream& os, const WriteStatus& status) {
  os << HistogramEnumString(status);
  return os;
}

std::ostream& operator<<(std::ostream& os, const WriteResult& s) {
  os << "{ status: " << s.status;
  if (s.status == WRITE_STATUS_OK) {
    os << ", bytes_written: " << s.bytes_written;
  } else {
    os << ", error_code: " << s.error_code;
  }
  os << " }";
  return os;
}

MessageResult::MessageResult(MessageStatus status, QuicMessageId message_id)
    : status(status), message_id(message_id) {}

#define RETURN_STRING_LITERAL(x) \
  case x:                        \
    return #x;

std::string QuicIetfTransportErrorCodeString(QuicIetfTransportErrorCodes c) {
  if (static_cast<uint64_t>(c) >= 0xff00u) {
    return quiche::QuicheStrCat("Private(", static_cast<uint64_t>(c), ")");
  }
  if (c >= CRYPTO_ERROR_FIRST && c <= CRYPTO_ERROR_LAST) {
    const int tls_error = static_cast<int>(c - CRYPTO_ERROR_FIRST);
    const char* tls_error_description = SSL_alert_desc_string_long(tls_error);
    if (strcmp("unknown", tls_error_description) != 0) {
      return quiche::QuicheStrCat("CRYPTO_ERROR(", tls_error_description, ")");
    }
    return quiche::QuicheStrCat("CRYPTO_ERROR(unknown(", tls_error, "))");
  }

  switch (c) {
    RETURN_STRING_LITERAL(NO_IETF_QUIC_ERROR);
    RETURN_STRING_LITERAL(INTERNAL_ERROR);
    RETURN_STRING_LITERAL(SERVER_BUSY_ERROR);
    RETURN_STRING_LITERAL(FLOW_CONTROL_ERROR);
    RETURN_STRING_LITERAL(STREAM_LIMIT_ERROR);
    RETURN_STRING_LITERAL(STREAM_STATE_ERROR);
    RETURN_STRING_LITERAL(FINAL_SIZE_ERROR);
    RETURN_STRING_LITERAL(FRAME_ENCODING_ERROR);
    RETURN_STRING_LITERAL(TRANSPORT_PARAMETER_ERROR);
    RETURN_STRING_LITERAL(CONNECTION_ID_LIMIT_ERROR);
    RETURN_STRING_LITERAL(PROTOCOL_VIOLATION);
    RETURN_STRING_LITERAL(INVALID_TOKEN);
    RETURN_STRING_LITERAL(CRYPTO_BUFFER_EXCEEDED);
    // CRYPTO_ERROR is handled in the if before this switch, these cases do not
    // change behavior and are only here to make the compiler happy.
    case CRYPTO_ERROR_FIRST:
    case CRYPTO_ERROR_LAST:
      DCHECK(false) << "Unexpected error " << static_cast<uint64_t>(c);
      break;
  }

  return quiche::QuicheStrCat("Unknown(", static_cast<uint64_t>(c), ")");
}

std::ostream& operator<<(std::ostream& os,
                         const QuicIetfTransportErrorCodes& c) {
  os << QuicIetfTransportErrorCodeString(c);
  return os;
}

QuicErrorCodeToIetfMapping QuicErrorCodeToTransportErrorCode(
    QuicErrorCode error) {
  switch (error) {
    // TODO(fkastenholz): Currently, all QuicError codes will map
    // to application error codes and the original Google QUIC error
    // code. This will change over time as we go through all calls to
    // CloseConnection() and see whether the call is a Transport or an
    // Application close and what the translated code should be.
    case QUIC_NO_ERROR:
      return {true, {static_cast<uint64_t>(QUIC_NO_ERROR)}};
    case QUIC_INTERNAL_ERROR:
      return {true, {static_cast<uint64_t>(QUIC_INTERNAL_ERROR)}};
    case QUIC_STREAM_DATA_AFTER_TERMINATION:
      return {true,
              {static_cast<uint64_t>(QUIC_STREAM_DATA_AFTER_TERMINATION)}};
    case QUIC_INVALID_PACKET_HEADER:
      return {true, {static_cast<uint64_t>(QUIC_INVALID_PACKET_HEADER)}};
    case QUIC_INVALID_FRAME_DATA:
      return {true, {static_cast<uint64_t>(QUIC_INVALID_FRAME_DATA)}};
    case QUIC_MISSING_PAYLOAD:
      return {true, {static_cast<uint64_t>(QUIC_MISSING_PAYLOAD)}};
    case QUIC_INVALID_FEC_DATA:
      return {true, {static_cast<uint64_t>(QUIC_INVALID_FEC_DATA)}};
    case QUIC_INVALID_STREAM_DATA:
      return {true, {static_cast<uint64_t>(QUIC_INVALID_STREAM_DATA)}};
    case QUIC_OVERLAPPING_STREAM_DATA:
      return {true, {static_cast<uint64_t>(QUIC_OVERLAPPING_STREAM_DATA)}};
    case QUIC_UNENCRYPTED_STREAM_DATA:
      return {true, {static_cast<uint64_t>(QUIC_UNENCRYPTED_STREAM_DATA)}};
    case QUIC_ATTEMPT_TO_SEND_UNENCRYPTED_STREAM_DATA:
      return {true,
              {static_cast<uint64_t>(
                  QUIC_ATTEMPT_TO_SEND_UNENCRYPTED_STREAM_DATA)}};
    case QUIC_MAYBE_CORRUPTED_MEMORY:
      return {true, {static_cast<uint64_t>(QUIC_MAYBE_CORRUPTED_MEMORY)}};
    case QUIC_UNENCRYPTED_FEC_DATA:
      return {true, {static_cast<uint64_t>(QUIC_UNENCRYPTED_FEC_DATA)}};
    case QUIC_INVALID_RST_STREAM_DATA:
      return {true, {static_cast<uint64_t>(QUIC_INVALID_RST_STREAM_DATA)}};
    case QUIC_INVALID_CONNECTION_CLOSE_DATA:
      return {true,
              {static_cast<uint64_t>(QUIC_INVALID_CONNECTION_CLOSE_DATA)}};
    case QUIC_INVALID_GOAWAY_DATA:
      return {true, {static_cast<uint64_t>(QUIC_INVALID_GOAWAY_DATA)}};
    case QUIC_INVALID_WINDOW_UPDATE_DATA:
      return {true, {static_cast<uint64_t>(QUIC_INVALID_WINDOW_UPDATE_DATA)}};
    case QUIC_INVALID_BLOCKED_DATA:
      return {true, {static_cast<uint64_t>(QUIC_INVALID_BLOCKED_DATA)}};
    case QUIC_INVALID_STOP_WAITING_DATA:
      return {true, {static_cast<uint64_t>(QUIC_INVALID_STOP_WAITING_DATA)}};
    case QUIC_INVALID_PATH_CLOSE_DATA:
      return {true, {static_cast<uint64_t>(QUIC_INVALID_PATH_CLOSE_DATA)}};
    case QUIC_INVALID_ACK_DATA:
      return {true, {static_cast<uint64_t>(QUIC_INVALID_ACK_DATA)}};
    case QUIC_INVALID_MESSAGE_DATA:
      return {true, {static_cast<uint64_t>(QUIC_INVALID_MESSAGE_DATA)}};
    case QUIC_INVALID_VERSION_NEGOTIATION_PACKET:
      return {true,
              {static_cast<uint64_t>(QUIC_INVALID_VERSION_NEGOTIATION_PACKET)}};
    case QUIC_INVALID_PUBLIC_RST_PACKET:
      return {true, {static_cast<uint64_t>(QUIC_INVALID_PUBLIC_RST_PACKET)}};
    case QUIC_DECRYPTION_FAILURE:
      return {true, {static_cast<uint64_t>(QUIC_DECRYPTION_FAILURE)}};
    case QUIC_ENCRYPTION_FAILURE:
      return {true, {static_cast<uint64_t>(QUIC_ENCRYPTION_FAILURE)}};
    case QUIC_PACKET_TOO_LARGE:
      return {true, {static_cast<uint64_t>(QUIC_PACKET_TOO_LARGE)}};
    case QUIC_PEER_GOING_AWAY:
      return {true, {static_cast<uint64_t>(QUIC_PEER_GOING_AWAY)}};
    case QUIC_INVALID_STREAM_ID:
      return {true, {static_cast<uint64_t>(QUIC_INVALID_STREAM_ID)}};
    case QUIC_INVALID_PRIORITY:
      return {true, {static_cast<uint64_t>(QUIC_INVALID_PRIORITY)}};
    case QUIC_TOO_MANY_OPEN_STREAMS:
      return {true, {static_cast<uint64_t>(QUIC_TOO_MANY_OPEN_STREAMS)}};
    case QUIC_TOO_MANY_AVAILABLE_STREAMS:
      return {true, {static_cast<uint64_t>(QUIC_TOO_MANY_AVAILABLE_STREAMS)}};
    case QUIC_PUBLIC_RESET:
      return {true, {static_cast<uint64_t>(QUIC_PUBLIC_RESET)}};
    case QUIC_INVALID_VERSION:
      return {true, {static_cast<uint64_t>(QUIC_INVALID_VERSION)}};
    case QUIC_INVALID_HEADER_ID:
      return {true, {static_cast<uint64_t>(QUIC_INVALID_HEADER_ID)}};
    case QUIC_INVALID_NEGOTIATED_VALUE:
      return {true, {static_cast<uint64_t>(QUIC_INVALID_NEGOTIATED_VALUE)}};
    case QUIC_DECOMPRESSION_FAILURE:
      return {true, {static_cast<uint64_t>(QUIC_DECOMPRESSION_FAILURE)}};
    case QUIC_NETWORK_IDLE_TIMEOUT:
      return {true, {static_cast<uint64_t>(QUIC_NETWORK_IDLE_TIMEOUT)}};
    case QUIC_HANDSHAKE_TIMEOUT:
      return {true, {static_cast<uint64_t>(QUIC_HANDSHAKE_TIMEOUT)}};
    case QUIC_ERROR_MIGRATING_ADDRESS:
      return {true, {static_cast<uint64_t>(QUIC_ERROR_MIGRATING_ADDRESS)}};
    case QUIC_ERROR_MIGRATING_PORT:
      return {true, {static_cast<uint64_t>(QUIC_ERROR_MIGRATING_PORT)}};
    case QUIC_PACKET_WRITE_ERROR:
      return {true, {static_cast<uint64_t>(QUIC_PACKET_WRITE_ERROR)}};
    case QUIC_PACKET_READ_ERROR:
      return {true, {static_cast<uint64_t>(QUIC_PACKET_READ_ERROR)}};
    case QUIC_EMPTY_STREAM_FRAME_NO_FIN:
      return {true, {static_cast<uint64_t>(QUIC_EMPTY_STREAM_FRAME_NO_FIN)}};
    case QUIC_INVALID_HEADERS_STREAM_DATA:
      return {true, {static_cast<uint64_t>(QUIC_INVALID_HEADERS_STREAM_DATA)}};
    case QUIC_HEADERS_STREAM_DATA_DECOMPRESS_FAILURE:
      return {
          true,
          {static_cast<uint64_t>(QUIC_HEADERS_STREAM_DATA_DECOMPRESS_FAILURE)}};
    case QUIC_FLOW_CONTROL_RECEIVED_TOO_MUCH_DATA:
      return {
          true,
          {static_cast<uint64_t>(QUIC_FLOW_CONTROL_RECEIVED_TOO_MUCH_DATA)}};
    case QUIC_FLOW_CONTROL_SENT_TOO_MUCH_DATA:
      return {true,
              {static_cast<uint64_t>(QUIC_FLOW_CONTROL_SENT_TOO_MUCH_DATA)}};
    case QUIC_FLOW_CONTROL_INVALID_WINDOW:
      return {true, {static_cast<uint64_t>(QUIC_FLOW_CONTROL_INVALID_WINDOW)}};
    case QUIC_CONNECTION_IP_POOLED:
      return {true, {static_cast<uint64_t>(QUIC_CONNECTION_IP_POOLED)}};
    case QUIC_TOO_MANY_OUTSTANDING_SENT_PACKETS:
      return {true,
              {static_cast<uint64_t>(QUIC_TOO_MANY_OUTSTANDING_SENT_PACKETS)}};
    case QUIC_TOO_MANY_OUTSTANDING_RECEIVED_PACKETS:
      return {
          true,
          {static_cast<uint64_t>(QUIC_TOO_MANY_OUTSTANDING_RECEIVED_PACKETS)}};
    case QUIC_CONNECTION_CANCELLED:
      return {true, {static_cast<uint64_t>(QUIC_CONNECTION_CANCELLED)}};
    case QUIC_BAD_PACKET_LOSS_RATE:
      return {true, {static_cast<uint64_t>(QUIC_BAD_PACKET_LOSS_RATE)}};
    case QUIC_PUBLIC_RESETS_POST_HANDSHAKE:
      return {true, {static_cast<uint64_t>(QUIC_PUBLIC_RESETS_POST_HANDSHAKE)}};
    case QUIC_FAILED_TO_SERIALIZE_PACKET:
      return {true, {static_cast<uint64_t>(QUIC_FAILED_TO_SERIALIZE_PACKET)}};
    case QUIC_TOO_MANY_RTOS:
      return {true, {static_cast<uint64_t>(QUIC_TOO_MANY_RTOS)}};
    case QUIC_HANDSHAKE_FAILED:
      return {true, {static_cast<uint64_t>(QUIC_HANDSHAKE_FAILED)}};
    case QUIC_CRYPTO_TAGS_OUT_OF_ORDER:
      return {true, {static_cast<uint64_t>(QUIC_CRYPTO_TAGS_OUT_OF_ORDER)}};
    case QUIC_CRYPTO_TOO_MANY_ENTRIES:
      return {true, {static_cast<uint64_t>(QUIC_CRYPTO_TOO_MANY_ENTRIES)}};
    case QUIC_CRYPTO_INVALID_VALUE_LENGTH:
      return {true, {static_cast<uint64_t>(QUIC_CRYPTO_INVALID_VALUE_LENGTH)}};
    case QUIC_CRYPTO_MESSAGE_AFTER_HANDSHAKE_COMPLETE:
      return {true,
              {static_cast<uint64_t>(
                  QUIC_CRYPTO_MESSAGE_AFTER_HANDSHAKE_COMPLETE)}};
    case QUIC_INVALID_CRYPTO_MESSAGE_TYPE:
      return {true, {static_cast<uint64_t>(QUIC_INVALID_CRYPTO_MESSAGE_TYPE)}};
    case QUIC_INVALID_CRYPTO_MESSAGE_PARAMETER:
      return {true,
              {static_cast<uint64_t>(QUIC_INVALID_CRYPTO_MESSAGE_PARAMETER)}};
    case QUIC_INVALID_CHANNEL_ID_SIGNATURE:
      return {true, {static_cast<uint64_t>(QUIC_INVALID_CHANNEL_ID_SIGNATURE)}};
    case QUIC_CRYPTO_MESSAGE_PARAMETER_NOT_FOUND:
      return {true,
              {static_cast<uint64_t>(QUIC_CRYPTO_MESSAGE_PARAMETER_NOT_FOUND)}};
    case QUIC_CRYPTO_MESSAGE_PARAMETER_NO_OVERLAP:
      return {
          true,
          {static_cast<uint64_t>(QUIC_CRYPTO_MESSAGE_PARAMETER_NO_OVERLAP)}};
    case QUIC_CRYPTO_MESSAGE_INDEX_NOT_FOUND:
      return {true,
              {static_cast<uint64_t>(QUIC_CRYPTO_MESSAGE_INDEX_NOT_FOUND)}};
    case QUIC_UNSUPPORTED_PROOF_DEMAND:
      return {true, {static_cast<uint64_t>(QUIC_UNSUPPORTED_PROOF_DEMAND)}};
    case QUIC_CRYPTO_INTERNAL_ERROR:
      return {true, {static_cast<uint64_t>(QUIC_CRYPTO_INTERNAL_ERROR)}};
    case QUIC_CRYPTO_VERSION_NOT_SUPPORTED:
      return {true, {static_cast<uint64_t>(QUIC_CRYPTO_VERSION_NOT_SUPPORTED)}};
    case QUIC_CRYPTO_NO_SUPPORT:
      return {true, {static_cast<uint64_t>(QUIC_CRYPTO_NO_SUPPORT)}};
    case QUIC_CRYPTO_TOO_MANY_REJECTS:
      return {true, {static_cast<uint64_t>(QUIC_CRYPTO_TOO_MANY_REJECTS)}};
    case QUIC_PROOF_INVALID:
      return {true, {static_cast<uint64_t>(QUIC_PROOF_INVALID)}};
    case QUIC_CRYPTO_DUPLICATE_TAG:
      return {true, {static_cast<uint64_t>(QUIC_CRYPTO_DUPLICATE_TAG)}};
    case QUIC_CRYPTO_ENCRYPTION_LEVEL_INCORRECT:
      return {true,
              {static_cast<uint64_t>(QUIC_CRYPTO_ENCRYPTION_LEVEL_INCORRECT)}};
    case QUIC_CRYPTO_SERVER_CONFIG_EXPIRED:
      return {true, {static_cast<uint64_t>(QUIC_CRYPTO_SERVER_CONFIG_EXPIRED)}};
    case QUIC_CRYPTO_SYMMETRIC_KEY_SETUP_FAILED:
      return {true,
              {static_cast<uint64_t>(QUIC_CRYPTO_SYMMETRIC_KEY_SETUP_FAILED)}};
    case QUIC_CRYPTO_MESSAGE_WHILE_VALIDATING_CLIENT_HELLO:
      return {true,
              {static_cast<uint64_t>(
                  QUIC_CRYPTO_MESSAGE_WHILE_VALIDATING_CLIENT_HELLO)}};
    case QUIC_CRYPTO_UPDATE_BEFORE_HANDSHAKE_COMPLETE:
      return {true,
              {static_cast<uint64_t>(
                  QUIC_CRYPTO_UPDATE_BEFORE_HANDSHAKE_COMPLETE)}};
    case QUIC_CRYPTO_CHLO_TOO_LARGE:
      return {true, {static_cast<uint64_t>(QUIC_CRYPTO_CHLO_TOO_LARGE)}};
    case QUIC_VERSION_NEGOTIATION_MISMATCH:
      return {true, {static_cast<uint64_t>(QUIC_VERSION_NEGOTIATION_MISMATCH)}};
    case QUIC_BAD_MULTIPATH_FLAG:
      return {true, {static_cast<uint64_t>(QUIC_BAD_MULTIPATH_FLAG)}};
    case QUIC_MULTIPATH_PATH_DOES_NOT_EXIST:
      return {true,
              {static_cast<uint64_t>(QUIC_MULTIPATH_PATH_DOES_NOT_EXIST)}};
    case QUIC_MULTIPATH_PATH_NOT_ACTIVE:
      return {true, {static_cast<uint64_t>(QUIC_MULTIPATH_PATH_NOT_ACTIVE)}};
    case QUIC_IP_ADDRESS_CHANGED:
      return {true, {static_cast<uint64_t>(QUIC_IP_ADDRESS_CHANGED)}};
    case QUIC_CONNECTION_MIGRATION_NO_MIGRATABLE_STREAMS:
      return {true,
              {static_cast<uint64_t>(
                  QUIC_CONNECTION_MIGRATION_NO_MIGRATABLE_STREAMS)}};
    case QUIC_CONNECTION_MIGRATION_TOO_MANY_CHANGES:
      return {
          true,
          {static_cast<uint64_t>(QUIC_CONNECTION_MIGRATION_TOO_MANY_CHANGES)}};
    case QUIC_CONNECTION_MIGRATION_NO_NEW_NETWORK:
      return {
          true,
          {static_cast<uint64_t>(QUIC_CONNECTION_MIGRATION_NO_NEW_NETWORK)}};
    case QUIC_CONNECTION_MIGRATION_NON_MIGRATABLE_STREAM:
      return {true,
              {static_cast<uint64_t>(
                  QUIC_CONNECTION_MIGRATION_NON_MIGRATABLE_STREAM)}};
    case QUIC_CONNECTION_MIGRATION_DISABLED_BY_CONFIG:
      return {true,
              {static_cast<uint64_t>(
                  QUIC_CONNECTION_MIGRATION_DISABLED_BY_CONFIG)}};
    case QUIC_CONNECTION_MIGRATION_INTERNAL_ERROR:
      return {
          true,
          {static_cast<uint64_t>(QUIC_CONNECTION_MIGRATION_INTERNAL_ERROR)}};
    case QUIC_CONNECTION_MIGRATION_HANDSHAKE_UNCONFIRMED:
      return {true,
              {static_cast<uint64_t>(
                  QUIC_CONNECTION_MIGRATION_HANDSHAKE_UNCONFIRMED)}};
    case QUIC_TOO_MANY_STREAM_DATA_INTERVALS:
      return {true,
              {static_cast<uint64_t>(QUIC_TOO_MANY_STREAM_DATA_INTERVALS)}};
    case QUIC_STREAM_SEQUENCER_INVALID_STATE:
      return {true,
              {static_cast<uint64_t>(QUIC_STREAM_SEQUENCER_INVALID_STATE)}};
    case QUIC_TOO_MANY_SESSIONS_ON_SERVER:
      return {true, {static_cast<uint64_t>(QUIC_TOO_MANY_SESSIONS_ON_SERVER)}};
    case QUIC_STREAM_LENGTH_OVERFLOW:
      return {true, {static_cast<uint64_t>(QUIC_STREAM_LENGTH_OVERFLOW)}};
    case QUIC_INVALID_MAX_DATA_FRAME_DATA:
      return {true, {static_cast<uint64_t>(QUIC_INVALID_MAX_DATA_FRAME_DATA)}};
    case QUIC_INVALID_MAX_STREAM_DATA_FRAME_DATA:
      return {true,
              {static_cast<uint64_t>(QUIC_INVALID_MAX_STREAM_DATA_FRAME_DATA)}};
    case QUIC_MAX_STREAMS_DATA:
      return {true, {static_cast<uint64_t>(QUIC_MAX_STREAMS_DATA)}};
    case QUIC_STREAMS_BLOCKED_DATA:
      return {true, {static_cast<uint64_t>(QUIC_STREAMS_BLOCKED_DATA)}};
    case QUIC_INVALID_STREAM_BLOCKED_DATA:
      return {true, {static_cast<uint64_t>(QUIC_INVALID_STREAM_BLOCKED_DATA)}};
    case QUIC_INVALID_NEW_CONNECTION_ID_DATA:
      return {true,
              {static_cast<uint64_t>(QUIC_INVALID_NEW_CONNECTION_ID_DATA)}};
    case QUIC_INVALID_STOP_SENDING_FRAME_DATA:
      return {true,
              {static_cast<uint64_t>(QUIC_INVALID_STOP_SENDING_FRAME_DATA)}};
    case QUIC_INVALID_PATH_CHALLENGE_DATA:
      return {true, {static_cast<uint64_t>(QUIC_INVALID_PATH_CHALLENGE_DATA)}};
    case QUIC_INVALID_PATH_RESPONSE_DATA:
      return {true, {static_cast<uint64_t>(QUIC_INVALID_PATH_RESPONSE_DATA)}};
    case IETF_QUIC_PROTOCOL_VIOLATION:
      return {true, {static_cast<uint64_t>(IETF_QUIC_PROTOCOL_VIOLATION)}};
    case QUIC_INVALID_NEW_TOKEN:
      return {true, {static_cast<uint64_t>(QUIC_INVALID_NEW_TOKEN)}};
    case QUIC_DATA_RECEIVED_ON_WRITE_UNIDIRECTIONAL_STREAM:
      return {true,
              {static_cast<uint64_t>(
                  QUIC_DATA_RECEIVED_ON_WRITE_UNIDIRECTIONAL_STREAM)}};
    case QUIC_TRY_TO_WRITE_DATA_ON_READ_UNIDIRECTIONAL_STREAM:
      return {true,
              {static_cast<uint64_t>(
                  QUIC_TRY_TO_WRITE_DATA_ON_READ_UNIDIRECTIONAL_STREAM)}};
    case QUIC_INVALID_RETIRE_CONNECTION_ID_DATA:
      return {true,
              {static_cast<uint64_t>(QUIC_INVALID_RETIRE_CONNECTION_ID_DATA)}};
    case QUIC_STREAMS_BLOCKED_ERROR:
      return {true, {static_cast<uint64_t>(QUIC_STREAMS_BLOCKED_ERROR)}};
    case QUIC_MAX_STREAMS_ERROR:
      return {true, {static_cast<uint64_t>(QUIC_MAX_STREAMS_ERROR)}};
    case QUIC_HTTP_DECODER_ERROR:
      return {true, {static_cast<uint64_t>(QUIC_HTTP_DECODER_ERROR)}};
    case QUIC_STALE_CONNECTION_CANCELLED:
      return {true, {static_cast<uint64_t>(QUIC_STALE_CONNECTION_CANCELLED)}};
    case QUIC_IETF_GQUIC_ERROR_MISSING:
      return {true, {static_cast<uint64_t>(QUIC_IETF_GQUIC_ERROR_MISSING)}};
    case QUIC_WINDOW_UPDATE_RECEIVED_ON_READ_UNIDIRECTIONAL_STREAM:
      return {true,
              {static_cast<uint64_t>(
                  QUIC_WINDOW_UPDATE_RECEIVED_ON_READ_UNIDIRECTIONAL_STREAM)}};
    case QUIC_TOO_MANY_BUFFERED_CONTROL_FRAMES:
      return {true,
              {static_cast<uint64_t>(QUIC_TOO_MANY_BUFFERED_CONTROL_FRAMES)}};
    case QUIC_TRANSPORT_INVALID_CLIENT_INDICATION:
      return {false, {0u}};
    case QUIC_QPACK_DECOMPRESSION_FAILED:
      return {
          false,
          {static_cast<uint64_t>(IETF_QUIC_HTTP_QPACK_DECOMPRESSION_FAILED)}};
    case QUIC_QPACK_ENCODER_STREAM_ERROR:
      return {
          false,
          {static_cast<uint64_t>(IETF_QUIC_HTTP_QPACK_ENCODER_STREAM_ERROR)}};
    case QUIC_QPACK_DECODER_STREAM_ERROR:
      return {
          false,
          {static_cast<uint64_t>(IETF_QUIC_HTTP_QPACK_DECODER_STREAM_ERROR)}};
    case QUIC_STREAM_DATA_BEYOND_CLOSE_OFFSET:
      return {true,
              {static_cast<uint64_t>(QUIC_STREAM_DATA_BEYOND_CLOSE_OFFSET)}};
    case QUIC_STREAM_MULTIPLE_OFFSET:
      return {true, {static_cast<uint64_t>(QUIC_STREAM_MULTIPLE_OFFSET)}};
    case QUIC_HTTP_FRAME_TOO_LARGE:
      return {false,
              {static_cast<uint64_t>(
                  QuicHttp3ErrorCode::IETF_QUIC_HTTP3_EXCESSIVE_LOAD)}};
    case QUIC_HTTP_FRAME_ERROR:
      return {false,
              {static_cast<uint64_t>(
                  QuicHttp3ErrorCode::IETF_QUIC_HTTP3_FRAME_ERROR)}};
    case QUIC_HTTP_FRAME_UNEXPECTED_ON_SPDY_STREAM:
      return {false,
              {static_cast<uint64_t>(
                  QuicHttp3ErrorCode::IETF_QUIC_HTTP3_FRAME_UNEXPECTED)}};
    case QUIC_HTTP_FRAME_UNEXPECTED_ON_CONTROL_STREAM:
      return {false,
              {static_cast<uint64_t>(
                  QuicHttp3ErrorCode::IETF_QUIC_HTTP3_FRAME_UNEXPECTED)}};
    case QUIC_HTTP_INVALID_FRAME_SEQUENCE_ON_SPDY_STREAM:
      return {false,
              {static_cast<uint64_t>(
                  QuicHttp3ErrorCode::IETF_QUIC_HTTP3_FRAME_UNEXPECTED)}};
    case QUIC_HTTP_INVALID_FRAME_SEQUENCE_ON_CONTROL_STREAM:
      return {false,
              {static_cast<uint64_t>(
                  QuicHttp3ErrorCode::IETF_QUIC_HTTP3_FRAME_UNEXPECTED)}};
    case QUIC_HTTP_DUPLICATE_UNIDIRECTIONAL_STREAM:
      return {false,
              {static_cast<uint64_t>(
                  QuicHttp3ErrorCode::IETF_QUIC_HTTP3_STREAM_CREATION_ERROR)}};
    case QUIC_HTTP_SERVER_INITIATED_BIDIRECTIONAL_STREAM:
      return {false,
              {static_cast<uint64_t>(
                  QuicHttp3ErrorCode::IETF_QUIC_HTTP3_STREAM_CREATION_ERROR)}};
    case QUIC_HTTP_STREAM_WRONG_DIRECTION:
      return {true, {static_cast<uint64_t>(STREAM_STATE_ERROR)}};
    case QUIC_HTTP_CLOSED_CRITICAL_STREAM:
      return {false,
              {static_cast<uint64_t>(
                  QuicHttp3ErrorCode::IETF_QUIC_HTTP3_CLOSED_CRITICAL_STREAM)}};
    case QUIC_HTTP_MISSING_SETTINGS_FRAME:
      return {false,
              {static_cast<uint64_t>(
                  QuicHttp3ErrorCode::IETF_QUIC_HTTP3_MISSING_SETTINGS)}};
    case QUIC_HTTP_DUPLICATE_SETTING_IDENTIFIER:
      return {false,
              {static_cast<uint64_t>(
                  QuicHttp3ErrorCode::IETF_QUIC_HTTP3_SETTINGS_ERROR)}};
    case QUIC_HPACK_INDEX_VARINT_ERROR:
      return {false, {static_cast<uint64_t>(QUIC_HPACK_INDEX_VARINT_ERROR)}};
    case QUIC_HPACK_NAME_LENGTH_VARINT_ERROR:
      return {false,
              {static_cast<uint64_t>(QUIC_HPACK_NAME_LENGTH_VARINT_ERROR)}};
    case QUIC_HPACK_VALUE_LENGTH_VARINT_ERROR:
      return {false,
              {static_cast<uint64_t>(QUIC_HPACK_VALUE_LENGTH_VARINT_ERROR)}};
    case QUIC_HPACK_NAME_TOO_LONG:
      return {false, {static_cast<uint64_t>(QUIC_HPACK_NAME_TOO_LONG)}};
    case QUIC_HPACK_VALUE_TOO_LONG:
      return {false, {static_cast<uint64_t>(QUIC_HPACK_VALUE_TOO_LONG)}};
    case QUIC_HPACK_NAME_HUFFMAN_ERROR:
      return {false, {static_cast<uint64_t>(QUIC_HPACK_NAME_HUFFMAN_ERROR)}};
    case QUIC_HPACK_VALUE_HUFFMAN_ERROR:
      return {false, {static_cast<uint64_t>(QUIC_HPACK_VALUE_HUFFMAN_ERROR)}};
    case QUIC_HPACK_MISSING_DYNAMIC_TABLE_SIZE_UPDATE:
      return {false,
              {static_cast<uint64_t>(
                  QUIC_HPACK_MISSING_DYNAMIC_TABLE_SIZE_UPDATE)}};
    case QUIC_HPACK_INVALID_INDEX:
      return {false, {static_cast<uint64_t>(QUIC_HPACK_INVALID_INDEX)}};
    case QUIC_HPACK_INVALID_NAME_INDEX:
      return {false, {static_cast<uint64_t>(QUIC_HPACK_INVALID_NAME_INDEX)}};
    case QUIC_HPACK_DYNAMIC_TABLE_SIZE_UPDATE_NOT_ALLOWED:
      return {false,
              {static_cast<uint64_t>(
                  QUIC_HPACK_DYNAMIC_TABLE_SIZE_UPDATE_NOT_ALLOWED)}};
    case QUIC_HPACK_INITIAL_TABLE_SIZE_UPDATE_IS_ABOVE_LOW_WATER_MARK:
      return {
          false,
          {static_cast<uint64_t>(
              QUIC_HPACK_INITIAL_TABLE_SIZE_UPDATE_IS_ABOVE_LOW_WATER_MARK)}};
    case QUIC_HPACK_TABLE_SIZE_UPDATE_IS_ABOVE_ACKNOWLEDGED_SETTING:
      return {false,
              {static_cast<uint64_t>(
                  QUIC_HPACK_TABLE_SIZE_UPDATE_IS_ABOVE_ACKNOWLEDGED_SETTING)}};
    case QUIC_HPACK_TRUNCATED_BLOCK:
      return {false, {static_cast<uint64_t>(QUIC_HPACK_TRUNCATED_BLOCK)}};
    case QUIC_HPACK_FRAGMENT_TOO_LONG:
      return {false, {static_cast<uint64_t>(QUIC_HPACK_FRAGMENT_TOO_LONG)}};
    case QUIC_HPACK_COMPRESSED_HEADER_SIZE_EXCEEDS_LIMIT:
      return {false,
              {static_cast<uint64_t>(
                  QUIC_HPACK_COMPRESSED_HEADER_SIZE_EXCEEDS_LIMIT)}};
    case QUIC_LAST_ERROR:
      return {false, {static_cast<uint64_t>(QUIC_LAST_ERROR)}};
  }
  // If it's an unknown code, indicate it's an application error code.
  return {false, {NO_IETF_QUIC_ERROR}};
}

std::string QuicIetfFrameTypeString(QuicIetfFrameType t) {
  if (IS_IETF_STREAM_FRAME(t)) {
    return "IETF_STREAM";
  }

  switch (t) {
    RETURN_STRING_LITERAL(IETF_PADDING);
    RETURN_STRING_LITERAL(IETF_PING);
    RETURN_STRING_LITERAL(IETF_ACK);
    RETURN_STRING_LITERAL(IETF_ACK_ECN);
    RETURN_STRING_LITERAL(IETF_RST_STREAM);
    RETURN_STRING_LITERAL(IETF_STOP_SENDING);
    RETURN_STRING_LITERAL(IETF_CRYPTO);
    RETURN_STRING_LITERAL(IETF_NEW_TOKEN);
    RETURN_STRING_LITERAL(IETF_MAX_DATA);
    RETURN_STRING_LITERAL(IETF_MAX_STREAM_DATA);
    RETURN_STRING_LITERAL(IETF_MAX_STREAMS_BIDIRECTIONAL);
    RETURN_STRING_LITERAL(IETF_MAX_STREAMS_UNIDIRECTIONAL);
    RETURN_STRING_LITERAL(IETF_DATA_BLOCKED);
    RETURN_STRING_LITERAL(IETF_STREAM_DATA_BLOCKED);
    RETURN_STRING_LITERAL(IETF_STREAMS_BLOCKED_BIDIRECTIONAL);
    RETURN_STRING_LITERAL(IETF_STREAMS_BLOCKED_UNIDIRECTIONAL);
    RETURN_STRING_LITERAL(IETF_NEW_CONNECTION_ID);
    RETURN_STRING_LITERAL(IETF_RETIRE_CONNECTION_ID);
    RETURN_STRING_LITERAL(IETF_PATH_CHALLENGE);
    RETURN_STRING_LITERAL(IETF_PATH_RESPONSE);
    RETURN_STRING_LITERAL(IETF_CONNECTION_CLOSE);
    RETURN_STRING_LITERAL(IETF_APPLICATION_CLOSE);
    RETURN_STRING_LITERAL(IETF_EXTENSION_MESSAGE_NO_LENGTH);
    RETURN_STRING_LITERAL(IETF_EXTENSION_MESSAGE);
    RETURN_STRING_LITERAL(IETF_EXTENSION_MESSAGE_NO_LENGTH_V99);
    RETURN_STRING_LITERAL(IETF_EXTENSION_MESSAGE_V99);
    default:
      return quiche::QuicheStrCat("Private value (", t, ")");
  }
}
std::ostream& operator<<(std::ostream& os, const QuicIetfFrameType& c) {
  os << QuicIetfFrameTypeString(c);
  return os;
}

std::string TransmissionTypeToString(TransmissionType transmission_type) {
  switch (transmission_type) {
    RETURN_STRING_LITERAL(NOT_RETRANSMISSION);
    RETURN_STRING_LITERAL(HANDSHAKE_RETRANSMISSION);
    RETURN_STRING_LITERAL(ALL_UNACKED_RETRANSMISSION);
    RETURN_STRING_LITERAL(ALL_INITIAL_RETRANSMISSION);
    RETURN_STRING_LITERAL(LOSS_RETRANSMISSION);
    RETURN_STRING_LITERAL(RTO_RETRANSMISSION);
    RETURN_STRING_LITERAL(TLP_RETRANSMISSION);
    RETURN_STRING_LITERAL(PTO_RETRANSMISSION);
    RETURN_STRING_LITERAL(PROBING_RETRANSMISSION);
    default:
      // Some varz rely on this behavior for statistic collection.
      if (transmission_type == LAST_TRANSMISSION_TYPE + 1) {
        return "INVALID_TRANSMISSION_TYPE";
      }
      return quiche::QuicheStrCat("Unknown(",
                                  static_cast<int>(transmission_type), ")");
      break;
  }
}

std::string PacketHeaderFormatToString(PacketHeaderFormat format) {
  switch (format) {
    RETURN_STRING_LITERAL(IETF_QUIC_LONG_HEADER_PACKET);
    RETURN_STRING_LITERAL(IETF_QUIC_SHORT_HEADER_PACKET);
    RETURN_STRING_LITERAL(GOOGLE_QUIC_PACKET);
    default:
      return quiche::QuicheStrCat("Unknown (", static_cast<int>(format), ")");
  }
}

std::string QuicLongHeaderTypeToString(QuicLongHeaderType type) {
  switch (type) {
    RETURN_STRING_LITERAL(VERSION_NEGOTIATION);
    RETURN_STRING_LITERAL(INITIAL);
    RETURN_STRING_LITERAL(ZERO_RTT_PROTECTED);
    RETURN_STRING_LITERAL(HANDSHAKE);
    RETURN_STRING_LITERAL(RETRY);
    RETURN_STRING_LITERAL(INVALID_PACKET_TYPE);
    default:
      return quiche::QuicheStrCat("Unknown (", static_cast<int>(type), ")");
  }
}

std::string MessageStatusToString(MessageStatus message_status) {
  switch (message_status) {
    RETURN_STRING_LITERAL(MESSAGE_STATUS_SUCCESS);
    RETURN_STRING_LITERAL(MESSAGE_STATUS_ENCRYPTION_NOT_ESTABLISHED);
    RETURN_STRING_LITERAL(MESSAGE_STATUS_UNSUPPORTED);
    RETURN_STRING_LITERAL(MESSAGE_STATUS_BLOCKED);
    RETURN_STRING_LITERAL(MESSAGE_STATUS_TOO_LARGE);
    RETURN_STRING_LITERAL(MESSAGE_STATUS_INTERNAL_ERROR);
    default:
      return quiche::QuicheStrCat("Unknown(", static_cast<int>(message_status),
                                  ")");
      break;
  }
}

std::string MessageResultToString(MessageResult message_result) {
  if (message_result.status != MESSAGE_STATUS_SUCCESS) {
    return quiche::QuicheStrCat(
        "{", MessageStatusToString(message_result.status), "}");
  }
  return quiche::QuicheStrCat(
      "{MESSAGE_STATUS_SUCCESS,id=", message_result.message_id, "}");
}

std::ostream& operator<<(std::ostream& os, const MessageResult& mr) {
  os << MessageResultToString(mr);
  return os;
}

std::string PacketNumberSpaceToString(PacketNumberSpace packet_number_space) {
  switch (packet_number_space) {
    RETURN_STRING_LITERAL(INITIAL_DATA);
    RETURN_STRING_LITERAL(HANDSHAKE_DATA);
    RETURN_STRING_LITERAL(APPLICATION_DATA);
    default:
      return quiche::QuicheStrCat("Unknown(",
                                  static_cast<int>(packet_number_space), ")");
      break;
  }
}

std::string SerializedPacketFateToString(SerializedPacketFate fate) {
  switch (fate) {
    RETURN_STRING_LITERAL(COALESCE);
    RETURN_STRING_LITERAL(BUFFER);
    RETURN_STRING_LITERAL(SEND_TO_WRITER);
    RETURN_STRING_LITERAL(FAILED_TO_WRITE_COALESCED_PACKET);
    default:
      return quiche::QuicheStrCat("Unknown(", static_cast<int>(fate), ")");
  }
}

std::string EncryptionLevelToString(EncryptionLevel level) {
  switch (level) {
    RETURN_STRING_LITERAL(ENCRYPTION_INITIAL);
    RETURN_STRING_LITERAL(ENCRYPTION_HANDSHAKE);
    RETURN_STRING_LITERAL(ENCRYPTION_ZERO_RTT);
    RETURN_STRING_LITERAL(ENCRYPTION_FORWARD_SECURE);
    default:
      return quiche::QuicheStrCat("Unknown(", static_cast<int>(level), ")");
      break;
  }
}

std::string QuicConnectionCloseTypeString(QuicConnectionCloseType type) {
  switch (type) {
    RETURN_STRING_LITERAL(GOOGLE_QUIC_CONNECTION_CLOSE);
    RETURN_STRING_LITERAL(IETF_QUIC_TRANSPORT_CONNECTION_CLOSE);
    RETURN_STRING_LITERAL(IETF_QUIC_APPLICATION_CONNECTION_CLOSE);
    default:
      return quiche::QuicheStrCat("Unknown(", static_cast<int>(type), ")");
      break;
  }
}

std::ostream& operator<<(std::ostream& os, const QuicConnectionCloseType type) {
  os << QuicConnectionCloseTypeString(type);
  return os;
}

std::string AddressChangeTypeToString(AddressChangeType type) {
  using IntType = typename std::underlying_type<AddressChangeType>::type;
  switch (type) {
    RETURN_STRING_LITERAL(NO_CHANGE);
    RETURN_STRING_LITERAL(PORT_CHANGE);
    RETURN_STRING_LITERAL(IPV4_SUBNET_CHANGE);
    RETURN_STRING_LITERAL(IPV4_TO_IPV4_CHANGE);
    RETURN_STRING_LITERAL(IPV4_TO_IPV6_CHANGE);
    RETURN_STRING_LITERAL(IPV6_TO_IPV4_CHANGE);
    RETURN_STRING_LITERAL(IPV6_TO_IPV6_CHANGE);
    default:
      return quiche::QuicheStrCat("Unknown(", static_cast<IntType>(type), ")");
  }
}

std::ostream& operator<<(std::ostream& os, AddressChangeType type) {
  os << AddressChangeTypeToString(type);
  return os;
}

}  // namespace quic
