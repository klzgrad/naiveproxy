// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_messages.h"

#include <string>

#include "quiche/quic/platform/api/quic_bug_tracker.h"

namespace moqt {

std::string MoqtMessageTypeToString(const MoqtMessageType message_type) {
  switch (message_type) {
    case MoqtMessageType::kObjectStream:
      return "OBJECT_STREAM";
    case MoqtMessageType::kObjectDatagram:
      return "OBJECT_PREFER_DATAGRAM";
    case MoqtMessageType::kClientSetup:
      return "CLIENT_SETUP";
    case MoqtMessageType::kServerSetup:
      return "SERVER_SETUP";
    case MoqtMessageType::kSubscribe:
      return "SUBSCRIBE_REQUEST";
    case MoqtMessageType::kSubscribeOk:
      return "SUBSCRIBE_OK";
    case MoqtMessageType::kSubscribeError:
      return "SUBSCRIBE_ERROR";
    case MoqtMessageType::kUnsubscribe:
      return "UNSUBSCRIBE";
    case MoqtMessageType::kSubscribeDone:
      return "SUBSCRIBE_DONE";
    case MoqtMessageType::kAnnounce:
      return "ANNOUNCE";
    case MoqtMessageType::kAnnounceOk:
      return "ANNOUNCE_OK";
    case MoqtMessageType::kAnnounceError:
      return "ANNOUNCE_ERROR";
    case MoqtMessageType::kUnannounce:
      return "UNANNOUNCE";
    case MoqtMessageType::kGoAway:
      return "GOAWAY";
    case MoqtMessageType::kStreamHeaderTrack:
      return "STREAM_HEADER_TRACK";
    case MoqtMessageType::kStreamHeaderGroup:
      return "STREAM_HEADER_GROUP";
  }
  return "Unknown message " + std::to_string(static_cast<int>(message_type));
}

std::string MoqtForwardingPreferenceToString(
    MoqtForwardingPreference preference) {
  switch (preference) {
    case MoqtForwardingPreference::kObject:
      return "OBJECT";
    case MoqtForwardingPreference::kDatagram:
      return "DATAGRAM";
    case MoqtForwardingPreference::kTrack:
      return "TRACK";
    case MoqtForwardingPreference::kGroup:
      return "GROUP";
  }
  QUIC_BUG(quic_bug_bad_moqt_message_type_01)
      << "Unknown preference " << std::to_string(static_cast<int>(preference));
  return "Unknown preference " + std::to_string(static_cast<int>(preference));
}

MoqtForwardingPreference GetForwardingPreference(MoqtMessageType type) {
  switch (type) {
    case MoqtMessageType::kObjectStream:
      return MoqtForwardingPreference::kObject;
    case MoqtMessageType::kObjectDatagram:
      return MoqtForwardingPreference::kDatagram;
    case MoqtMessageType::kStreamHeaderTrack:
      return MoqtForwardingPreference::kTrack;
    case MoqtMessageType::kStreamHeaderGroup:
      return MoqtForwardingPreference::kGroup;
    default:
      break;
  }
  QUIC_BUG(quic_bug_bad_moqt_message_type_02)
      << "Message type does not indicate forwarding preference";
  return MoqtForwardingPreference::kObject;
};

MoqtMessageType GetMessageTypeForForwardingPreference(
    MoqtForwardingPreference preference) {
  switch (preference) {
    case MoqtForwardingPreference::kObject:
      return MoqtMessageType::kObjectStream;
    case MoqtForwardingPreference::kDatagram:
      return MoqtMessageType::kObjectDatagram;
    case MoqtForwardingPreference::kTrack:
      return MoqtMessageType::kStreamHeaderTrack;
    case MoqtForwardingPreference::kGroup:
      return MoqtMessageType::kStreamHeaderGroup;
  }
  QUIC_BUG(quic_bug_bad_moqt_message_type_03)
      << "Forwarding preference does not indicate message type";
  return MoqtMessageType::kObjectStream;
}

}  // namespace moqt
