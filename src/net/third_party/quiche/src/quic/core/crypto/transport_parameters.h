// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_TRANSPORT_PARAMETERS_H_
#define QUICHE_QUIC_CORE_CRYPTO_TRANSPORT_PARAMETERS_H_

#include <memory>
#include <vector>

#include "third_party/boringssl/src/include/openssl/bytestring.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_handshake_message.h"
#include "net/third_party/quiche/src/quic/core/quic_connection_id.h"
#include "net/third_party/quiche/src/quic/core/quic_data_writer.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_containers.h"

namespace quic {

// TransportParameters contains parameters for QUIC's transport layer that are
// exchanged during the TLS handshake. This struct is a mirror of the struct in
// the "Transport Parameter Encoding" section of draft-ietf-quic-transport.
// This struct currently uses the values from draft 20.
struct QUIC_EXPORT_PRIVATE TransportParameters {
  // The identifier used to differentiate transport parameters.
  enum TransportParameterId : uint16_t;
  // A map used to specify custom parameters.
  using ParameterMap = QuicUnorderedMap<TransportParameterId, std::string>;
  // Represents an individual QUIC transport parameter that only encodes a
  // variable length integer. Can only be created inside the constructor for
  // TransportParameters.
  class QUIC_EXPORT_PRIVATE IntegerParameter {
   public:
    // Forbid constructing and copying apart from TransportParameters.
    IntegerParameter() = delete;
    IntegerParameter(const IntegerParameter&) = delete;
    IntegerParameter& operator=(const IntegerParameter&) = delete;
    // Sets the value of this transport parameter.
    void set_value(uint64_t value);
    // Gets the value of this transport parameter.
    uint64_t value() const;
    // Validates whether the current value is valid.
    bool IsValid() const;
    // Writes to a crypto byte buffer, used during serialization. Does not write
    // anything if the value is equal to the parameter's default value.
    // Returns whether the write was successful.
    bool WriteToCbb(CBB* parent_cbb) const;
    // Reads from a crypto byte string, used during parsing.
    // Returns whether the read was successful.
    bool ReadFromCbs(CBS* const value_cbs);
    // operator<< allows easily logging integer transport parameters.
    friend QUIC_EXPORT_PRIVATE std::ostream& operator<<(
        std::ostream& os,
        const IntegerParameter& param);

   private:
    friend struct TransportParameters;
    // Constructors for initial setup used by TransportParameters only.
    // This constructor sets |default_value| and |min_value| to 0, and
    // |max_value| to kVarInt62MaxValue.
    explicit IntegerParameter(TransportParameterId param_id);
    IntegerParameter(TransportParameterId param_id,
                     uint64_t default_value,
                     uint64_t min_value,
                     uint64_t max_value);
    // Human-readable string representation.
    std::string ToString(bool for_use_in_list) const;

    // Number used to indicate this transport parameter.
    TransportParameterId param_id_;
    // Current value of the transport parameter.
    uint64_t value_;
    // Default value of this transport parameter, as per IETF specification.
    const uint64_t default_value_;
    // Minimum value of this transport parameter, as per IETF specification.
    const uint64_t min_value_;
    // Maximum value of this transport parameter, as per IETF specification.
    const uint64_t max_value_;
    // Ensures this parameter is not parsed twice in the same message.
    bool has_been_read_from_cbs_;
  };

  // Represents the preferred_address transport parameter that a server can
  // send to clients.
  struct QUIC_EXPORT_PRIVATE PreferredAddress {
    PreferredAddress();
    ~PreferredAddress();

    QuicSocketAddress ipv4_socket_address;
    QuicSocketAddress ipv6_socket_address;
    QuicConnectionId connection_id;
    std::vector<uint8_t> stateless_reset_token;

    // Allows easily logging.
    std::string ToString() const;
    friend QUIC_EXPORT_PRIVATE std::ostream& operator<<(
        std::ostream& os,
        const TransportParameters& params);
  };

  TransportParameters();
  ~TransportParameters();

  // Represents the sender of the transport parameters. When |perspective| is
  // Perspective::IS_CLIENT, this struct is being used in the client_hello
  // handshake message; when it is Perspective::IS_SERVER, it is being used in
  // the encrypted_extensions handshake message.
  Perspective perspective;

  // When Perspective::IS_CLIENT, |version| is the initial version offered by
  // the client (before any version negotiation packets) for this connection.
  // When Perspective::IS_SERVER, |version| is the version that is in use.
  QuicVersionLabel version;

  // |supported_versions| contains a list of all versions that the server would
  // send in a version negotiation packet. It is not used if |perspective ==
  // Perspective::IS_CLIENT|.
  QuicVersionLabelVector supported_versions;

  // The value of the Destination Connection ID field from the first
  // Initial packet sent by the client.
  QuicConnectionId original_connection_id;

  // Idle timeout expressed in milliseconds.
  IntegerParameter idle_timeout_milliseconds;

  // Stateless reset token used in verifying stateless resets.
  std::vector<uint8_t> stateless_reset_token;

  // Limits the size of packets that the endpoint is willing to receive.
  // This indicates that packets larger than this limit will be dropped.
  IntegerParameter max_packet_size;

  // Contains the initial value for the maximum amount of data that can
  // be sent on the connection.
  IntegerParameter initial_max_data;

  // Initial flow control limit for locally-initiated bidirectional streams.
  IntegerParameter initial_max_stream_data_bidi_local;

  // Initial flow control limit for peer-initiated bidirectional streams.
  IntegerParameter initial_max_stream_data_bidi_remote;

  // Initial flow control limit for unidirectional streams.
  IntegerParameter initial_max_stream_data_uni;

  // Initial maximum number of bidirectional streams the peer may initiate.
  IntegerParameter initial_max_streams_bidi;

  // Initial maximum number of unidirectional streams the peer may initiate.
  IntegerParameter initial_max_streams_uni;

  // Exponent used to decode the ACK Delay field in ACK frames.
  IntegerParameter ack_delay_exponent;

  // Maximum amount of time in milliseconds by which the endpoint will
  // delay sending acknowledgments.
  IntegerParameter max_ack_delay;

  // Indicates lack of support for connection migration.
  bool disable_migration;

  // Used to effect a change in server address at the end of the handshake.
  std::unique_ptr<PreferredAddress> preferred_address;

  // Maximum number of connection IDs from the peer that an endpoint is willing
  // to store.
  IntegerParameter active_connection_id_limit;

  // Transport parameters used by Google QUIC but not IETF QUIC. This is
  // serialized into a TransportParameter struct with a TransportParameterId of
  // kGoogleQuicParamId.
  std::unique_ptr<CryptoHandshakeMessage> google_quic_params;

  // Validates whether transport parameters are valid according to
  // the specification.
  bool AreValid() const;

  // Custom parameters that may be specific to application protocol.
  ParameterMap custom_parameters;

  // Allows easily logging transport parameters.
  std::string ToString() const;
  friend QUIC_EXPORT_PRIVATE std::ostream& operator<<(
      std::ostream& os,
      const TransportParameters& params);
};

// Serializes a TransportParameters struct into the format for sending it in a
// TLS extension. The serialized bytes are written to |*out|. Returns if the
// parameters are valid and serialization succeeded.
QUIC_EXPORT_PRIVATE bool SerializeTransportParameters(
    ParsedQuicVersion version,
    const TransportParameters& in,
    std::vector<uint8_t>* out);

// Parses bytes from the quic_transport_parameters TLS extension and writes the
// parsed parameters into |*out|. Input is read from |in| for |in_len| bytes.
// |perspective| indicates whether the input came from a client or a server.
// This method returns true if the input was successfully parsed.
// TODO(nharper): Write fuzz tests for this method.
QUIC_EXPORT_PRIVATE bool ParseTransportParameters(ParsedQuicVersion version,
                                                  Perspective perspective,
                                                  const uint8_t* in,
                                                  size_t in_len,
                                                  TransportParameters* out);

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_TRANSPORT_PARAMETERS_H_
