// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_CORE_QUIC_CONNECTION_ID_H_
#define NET_THIRD_PARTY_QUIC_CORE_QUIC_CONNECTION_ID_H_

#include "net/third_party/quic/platform/api/quic_export.h"
#include "net/third_party/quic/platform/api/quic_string.h"
#include "net/third_party/quic/platform/api/quic_uint128.h"

namespace quic {

enum QuicConnectionIdLength {
  PACKET_0BYTE_CONNECTION_ID = 0,
  PACKET_8BYTE_CONNECTION_ID = 8,
};

// Connection IDs can be 0-18 bytes per IETF specifications.
const uint8_t kQuicMaxConnectionIdLength = 18;

// kQuicDefaultConnectionIdLength is the only supported length for QUIC
// versions < v99, and is the default picked for all versions.
const uint8_t kQuicDefaultConnectionIdLength = 8;

class QUIC_EXPORT_PRIVATE QuicConnectionId {
 public:
  // Creates a connection ID of length zero, unless the restart flag
  // quic_connection_ids_network_byte_order is false in which case
  // it returns an 8-byte all-zeroes connection ID.
  QuicConnectionId();

  // Creates a connection ID from network order bytes.
  QuicConnectionId(const char* data, uint8_t length);

  // Creator from host byte order uint64_t.
  explicit QuicConnectionId(uint64_t connection_id64);

  ~QuicConnectionId();

  // Returns the length of the connection ID, in bytes.
  uint8_t length() const;

  // Sets the length of the connection ID, in bytes.
  void set_length(uint8_t length);

  // Returns a pointer to the connection ID bytes, in network byte order.
  const char* data() const;

  // Returns a mutable pointer to the connection ID bytes,
  // in network byte order.
  char* mutable_data();

  // Returns whether the connection ID has length zero, unless the restart flag
  // quic_connection_ids_network_byte_order is false in which case
  // it checks if it is all zeroes.
  bool IsEmpty() const;

  // Converts to host byte order uint64_t.
  uint64_t ToUInt64() const;

  // Hash() is required to use connection IDs as keys in hash tables.
  size_t Hash() const;

  // Generates an ASCII string that represents
  // the contents of the connection ID, or "0" if it is empty.
  QuicString ToString() const;

  // operator<< allows easily logging connection IDs.
  friend QUIC_EXPORT_PRIVATE std::ostream& operator<<(
      std::ostream& os,
      const QuicConnectionId& v);

  bool operator==(const QuicConnectionId& v) const;
  bool operator!=(const QuicConnectionId& v) const;
  // operator< is required to use connection IDs as keys in hash tables.
  bool operator<(const QuicConnectionId& v) const;

 private:
  // The connection ID is currently represented in host byte order in |id64_|.
  // In the future, it will be saved in the first |length_| bytes of |data_|.
  char data_[kQuicMaxConnectionIdLength];
  uint8_t length_;
  uint64_t id64_;  // host byte order
};

// Creates a connection ID of length zero, unless the restart flag
// quic_connection_ids_network_byte_order is false in which case
// it returns an 8-byte all-zeroes connection ID.
QUIC_EXPORT_PRIVATE QuicConnectionId EmptyQuicConnectionId();

// Converts connection ID from host-byte-order uint64_t to QuicConnectionId.
// This is currently the identity function.
QUIC_EXPORT_PRIVATE QuicConnectionId
QuicConnectionIdFromUInt64(uint64_t connection_id64);

// Converts connection ID from QuicConnectionId to host-byte-order uint64_t.
// This is currently the identity function.
QUIC_EXPORT_PRIVATE uint64_t
QuicConnectionIdToUInt64(QuicConnectionId connection_id);

// QuicConnectionIdHash can be passed as hash argument to hash tables.
class QuicConnectionIdHash {
 public:
  size_t operator()(QuicConnectionId const& connection_id) const noexcept {
    return connection_id.Hash();
  }
};

// Governs how connection IDs are represented in memory.
// Checks gfe_restart_flag_quic_connection_ids_network_byte_order.
QUIC_EXPORT_PRIVATE bool QuicConnectionIdUseNetworkByteOrder();

enum class Perspective : uint8_t;
// Governs how connection IDs are created.
// Checks gfe_restart_flag_quic_variable_length_connection_ids_(client|server).
QUIC_EXPORT_PRIVATE bool QuicConnectionIdSupportsVariableLength(
    Perspective perspective);

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_CORE_QUIC_CONNECTION_ID_H_
