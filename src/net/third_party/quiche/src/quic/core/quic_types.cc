// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_types.h"

namespace quic {

QuicConsumedData::QuicConsumedData(size_t bytes_consumed, bool fin_consumed)
    : bytes_consumed(bytes_consumed), fin_consumed(fin_consumed) {}

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
    case WRITE_STATUS_NUM_VALUES:
      return "NUM_VALUES";
  }
  QUIC_DLOG(ERROR) << "Invalid WriteStatus value: " << enum_value;
  return "<invalid>";
}

WriteResult::WriteResult() : status(WRITE_STATUS_ERROR), bytes_written(0) {}

WriteResult::WriteResult(WriteStatus status, int bytes_written_or_error_code)
    : status(status), bytes_written(bytes_written_or_error_code) {}

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

std::ostream& operator<<(std::ostream& os,
                         const QuicIetfTransportErrorCodes& c) {
  if (static_cast<uint16_t>(c) >= 0xff00u) {
    os << "Private value: " << c;
    return os;
  }

  switch (c) {
    case NO_IETF_QUIC_ERROR:
      os << "NO_IETF_QUIC_ERROR";
      break;
    case INTERNAL_ERROR:
      os << "INTERNAL_ERROR";
      break;
    case SERVER_BUSY_ERROR:
      os << "SERVER_BUSY_ERROR";
      break;
    case FLOW_CONTROL_ERROR:
      os << "FLOW_CONTROL_ERROR";
      break;
    case STREAM_LIMIT_ERROR:
      os << "STREAM_LIMIT_ERROR";
      break;
    case STREAM_STATE_ERROR:
      os << "STREAM_STATE_ERROR";
      break;
    case FINAL_SIZE_ERROR:
      os << "FINAL_SIZE_ERROR";
      break;
    case FRAME_ENCODING_ERROR:
      os << "FRAME_ENCODING_ERROR";
      break;
    case TRANSPORT_PARAMETER_ERROR:
      os << "TRANSPORT_PARAMETER_ERROR";
      break;
    case VERSION_NEGOTIATION_ERROR:
      os << "VERSION_NEGOTIATION_ERROR";
      break;
    case PROTOCOL_VIOLATION:
      os << "PROTOCOL_VIOLATION";
      break;
    case INVALID_MIGRATION:
      os << "INVALID_MIGRATION";
      break;
      // No default -- compiler will catch any adds/changes then.
  }
  return os;
}

}  // namespace quic
