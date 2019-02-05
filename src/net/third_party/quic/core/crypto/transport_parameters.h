// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_CORE_CRYPTO_TRANSPORT_PARAMETERS_H_
#define NET_THIRD_PARTY_QUIC_CORE_CRYPTO_TRANSPORT_PARAMETERS_H_

#include <memory>
#include <vector>

#include "net/third_party/quic/core/crypto/crypto_handshake_message.h"
#include "net/third_party/quic/core/quic_types.h"
#include "net/third_party/quic/core/quic_versions.h"

namespace quic {

// TransportParameters contains parameters for QUIC's transport layer that are
// indicated during the TLS handshake. This struct is a mirror of the struct in
// section 6.4 of draft-ietf-quic-transport-11.
struct QUIC_EXPORT_PRIVATE TransportParameters {
  TransportParameters();
  ~TransportParameters();

  // When |perspective| is Perspective::IS_CLIENT, this struct is being used in
  // the client_hello handshake message; when it is Perspective::IS_SERVER, it
  // is being used in the encrypted_extensions handshake message.
  Perspective perspective;

  // When Perspective::IS_CLIENT, |version| is the initial version offered by
  // the client (before any version negotiation packets) for this connection.
  // When Perspective::IS_SERVER, |version| is the version that is in use.
  QuicVersionLabel version = 0;

  // Server-only parameters:

  // |supported_versions| contains a list of all versions that the server would
  // send in a version negotiation packet. It is not used if |perspective ==
  // Perspective::IS_CLIENT|.
  QuicVersionLabelVector supported_versions;

  // See section 6.4.1 of draft-ietf-quic-transport-11 for definition.
  std::vector<uint8_t> stateless_reset_token;

  // Required parameters. See section 6.4.1 of draft-ietf-quic-transport-11 for
  // definitions.
  uint32_t initial_max_stream_data = 0;
  uint32_t initial_max_data = 0;
  uint16_t idle_timeout = 0;

  template <typename T>
  struct OptionalParam {
    bool present = false;
    T value;
  };

  // Optional parameters. See section 6.4.1 of draft-ietf-quic-transport-11 for
  // definitions.
  OptionalParam<uint16_t> initial_max_bidi_streams;
  OptionalParam<uint16_t> initial_max_uni_streams;
  OptionalParam<uint16_t> max_packet_size;
  OptionalParam<uint8_t> ack_delay_exponent;

  // Transport parameters used by Google QUIC but not IETF QUIC. This is
  // serialized into a TransportParameter struct with a TransportParameterId of
  // 18257.
  std::unique_ptr<CryptoHandshakeMessage> google_quic_params;

  // Returns true if the contents of this struct are valid.
  bool is_valid() const;
};

// Serializes a TransportParameters struct into the format for sending it in a
// TLS extension. The serialized bytes are put in |*out|, and this function
// returns true on success or false if |TransportParameters::is_valid| returns
// false.
QUIC_EXPORT_PRIVATE bool SerializeTransportParameters(
    const TransportParameters& in,
    std::vector<uint8_t>* out);

// Parses bytes from the quic_transport_parameters TLS extension and writes the
// parsed parameters into |*out|. Input is read from |in| for |in_len| bytes.
// |perspective| indicates whether the input came from a client or a server.
// This method returns true if the input was successfully parsed, and false if
// it could not be parsed.
QUIC_EXPORT_PRIVATE bool ParseTransportParameters(const uint8_t* in,
                                                  size_t in_len,
                                                  Perspective perspective,
                                                  TransportParameters* out);

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_CORE_CRYPTO_TRANSPORT_PARAMETERS_H_
