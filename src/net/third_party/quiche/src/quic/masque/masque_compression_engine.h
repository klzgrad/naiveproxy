// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MASQUE_MASQUE_PROTOCOL_H_
#define QUICHE_QUIC_MASQUE_MASQUE_PROTOCOL_H_

#include "net/third_party/quiche/src/quic/core/quic_connection_id.h"
#include "net/third_party/quiche/src/quic/core/quic_session.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_containers.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_socket_address.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

// MASQUE compression engine used by client and servers.
// This class allows converting QUIC packets into a compressed form suitable
// for sending over QUIC DATAGRAM frames. It leverages a flow identifier at the
// start of each datagram to indicate which compression context was used to
// compress this packet, or to create new compression contexts.
// Compression contexts contain client and server connection IDs and the
// server's IP and port. This allows compressing that information in most
// packets without requiring access to the cryptographic keys of the end-to-end
// encapsulated session. When the flow identifier is 0, the DATAGRAM contains
// all the contents of the compression context. When the flow identifier is
// non-zero, those fields are removed so the encapsulated QUIC packet is
// transmitted without connection IDs and reassembled by the peer on
// decompression. This only needs to contain the HTTP server's IP address since
// the client's IP address is not visible to the HTTP server.

class QUIC_NO_EXPORT MasqueCompressionEngine {
 public:
  // Caller must ensure that |masque_session| has a lifetime longer than the
  // newly constructed MasqueCompressionEngine.
  explicit MasqueCompressionEngine(QuicSession* masque_session);

  // Disallow copy and assign.
  MasqueCompressionEngine(const MasqueCompressionEngine&) = delete;
  MasqueCompressionEngine& operator=(const MasqueCompressionEngine&) = delete;

  // Compresses packet and sends it in a DATAGRAM frame over a MASQUE session.
  // When used from MASQUE client to MASQUE server, the MASQUE server will then
  // send the packet to the provided |server_address|.
  // When used from MASQUE server to MASQUE client, the MASQUE client will then
  // hand off the uncompressed packet to an encapsulated session that will treat
  // it as having come from the provided |server_address|.
  // The connection IDs are the one used by the encapsulated |packet|.
  void CompressAndSendPacket(quiche::QuicheStringPiece packet,
                             QuicConnectionId client_connection_id,
                             QuicConnectionId server_connection_id,
                             const QuicSocketAddress& server_address);

  // Decompresses received DATAGRAM frame contents from |datagram| and places
  // them in |packet|. Reverses the transformation from CompressAndSendPacket.
  // The connection IDs are the one used by the encapsulated |packet|.
  // |server_address| will be filled with the |server_address| passed to
  // CompressAndSendPacket. |version_present| will contain whether the
  // encapsulated |packet| contains a Version field.
  bool DecompressDatagram(quiche::QuicheStringPiece datagram,
                          QuicConnectionId* client_connection_id,
                          QuicConnectionId* server_connection_id,
                          QuicSocketAddress* server_address,
                          std::vector<char>* packet,
                          bool* version_present);

  // Clears all entries referencing |client_connection_id| from the
  // compression table.
  void UnregisterClientConnectionId(QuicConnectionId client_connection_id);

 private:
  struct QUIC_NO_EXPORT MasqueCompressionContext {
    QuicConnectionId client_connection_id;
    QuicConnectionId server_connection_id;
    QuicSocketAddress server_address;
    bool validated = false;
  };

  // Generates a new datagram flow ID.
  QuicDatagramFlowId GetNextFlowId();

  // Finds or creates a new compression context to use during compression.
  // |client_connection_id_present| and |server_connection_id_present| indicate
  // whether the corresponding connection ID is present in the current packet.
  // |validated| will contain whether the compression context that matches
  // these arguments is currently validated or not.
  QuicDatagramFlowId FindOrCreateCompressionContext(
      QuicConnectionId client_connection_id,
      QuicConnectionId server_connection_id,
      const QuicSocketAddress& server_address,
      bool client_connection_id_present,
      bool server_connection_id_present,
      bool* validated);

  // Writes compressed packet to |slice| during compression.
  bool WriteCompressedPacketToSlice(QuicConnectionId client_connection_id,
                                    QuicConnectionId server_connection_id,
                                    const QuicSocketAddress& server_address,
                                    QuicConnectionId destination_connection_id,
                                    QuicConnectionId source_connection_id,
                                    QuicDatagramFlowId flow_id,
                                    bool validated,
                                    uint8_t first_byte,
                                    bool long_header,
                                    QuicDataReader* reader,
                                    QuicDataWriter* writer);

  // Parses compression context from flow ID 0 during decompression.
  bool ParseCompressionContext(QuicDataReader* reader,
                               MasqueCompressionContext* context);

  // Writes decompressed packet to |packet| during decompression.
  bool WriteDecompressedPacket(QuicDataReader* reader,
                               const MasqueCompressionContext& context,
                               std::vector<char>* packet,
                               bool* version_present);

  QuicSession* masque_session_;  // Unowned.
  QuicUnorderedMap<QuicDatagramFlowId, MasqueCompressionContext> contexts_;
  QuicDatagramFlowId next_flow_id_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_MASQUE_MASQUE_PROTOCOL_H_
