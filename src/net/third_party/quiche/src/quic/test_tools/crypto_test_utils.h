// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_CRYPTO_TEST_UTILS_H_
#define QUICHE_QUIC_TEST_TOOLS_CRYPTO_TEST_UTILS_H_

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "third_party/boringssl/src/include/openssl/evp.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_framer.h"
#include "net/third_party/quiche/src/quic/core/quic_framer.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

class CommonCertSets;
class ProofSource;
class ProofVerifier;
class ProofVerifyContext;
class QuicClock;
class QuicConfig;
class QuicCryptoClientStream;
class QuicCryptoServerConfig;
class QuicCryptoServerStreamBase;
class QuicCryptoStream;
class QuicRandom;
class QuicServerId;

namespace test {

class PacketSavingConnection;

namespace crypto_test_utils {

// An interface for a source of callbacks. This is used for invoking
// callbacks asynchronously.
//
// Call the RunPendingCallbacks method regularly to run the callbacks from
// this source.
class CallbackSource {
 public:
  virtual ~CallbackSource() {}

  // Runs pending callbacks from this source. If there is no pending
  // callback, does nothing.
  virtual void RunPendingCallbacks() = 0;
};

// FakeClientOptions bundles together a number of options for configuring
// HandshakeWithFakeClient.
struct FakeClientOptions {
  FakeClientOptions();
  ~FakeClientOptions();

  // If only_tls_versions is set, then the client will only use TLS for the
  // crypto handshake.
  bool only_tls_versions = false;

  // If only_quic_crypto_versions is set, then the client will only use
  // PROTOCOL_QUIC_CRYPTO for the crypto handshake.
  bool only_quic_crypto_versions = false;
};

// Returns a QuicCryptoServerConfig that is in a reasonable configuration to
// pass into HandshakeWithFakeServer.
std::unique_ptr<QuicCryptoServerConfig> CryptoServerConfigForTesting();

// returns: the number of client hellos that the client sent.
int HandshakeWithFakeServer(QuicConfig* server_quic_config,
                            QuicCryptoServerConfig* crypto_config,
                            MockQuicConnectionHelper* helper,
                            MockAlarmFactory* alarm_factory,
                            PacketSavingConnection* client_conn,
                            QuicCryptoClientStream* client,
                            std::string alpn);

// returns: the number of client hellos that the client sent.
int HandshakeWithFakeClient(MockQuicConnectionHelper* helper,
                            MockAlarmFactory* alarm_factory,
                            PacketSavingConnection* server_conn,
                            QuicCryptoServerStreamBase* server,
                            const QuicServerId& server_id,
                            const FakeClientOptions& options,
                            std::string alpn);

// SetupCryptoServerConfigForTest configures |crypto_config|
// with sensible defaults for testing.
void SetupCryptoServerConfigForTest(const QuicClock* clock,
                                    QuicRandom* rand,
                                    QuicCryptoServerConfig* crypto_config);

// Sends the handshake message |message| to stream |stream| with the perspective
// that the message is coming from |perspective|.
void SendHandshakeMessageToStream(QuicCryptoStream* stream,
                                  const CryptoHandshakeMessage& message,
                                  Perspective perspective);

// CommunicateHandshakeMessages moves messages from |client| to |server| and
// back until |clients|'s handshake has completed.
void CommunicateHandshakeMessages(PacketSavingConnection* client_conn,
                                  QuicCryptoStream* client,
                                  PacketSavingConnection* server_conn,
                                  QuicCryptoStream* server);

// AdvanceHandshake attempts to moves messages from |client| to |server| and
// |server| to |client|. Returns the number of messages moved.
std::pair<size_t, size_t> AdvanceHandshake(PacketSavingConnection* client_conn,
                                           QuicCryptoStream* client,
                                           size_t client_i,
                                           PacketSavingConnection* server_conn,
                                           QuicCryptoStream* server,
                                           size_t server_i);

// Returns the value for the tag |tag| in the tag value map of |message|.
std::string GetValueForTag(const CryptoHandshakeMessage& message, QuicTag tag);

// Returns a new |ProofSource| that serves up test certificates.
std::unique_ptr<ProofSource> ProofSourceForTesting();

// Returns a new |ProofVerifier| that uses the QUIC testing root CA.
std::unique_ptr<ProofVerifier> ProofVerifierForTesting();

// Returns a hash of the leaf test certificate.
uint64_t LeafCertHashForTesting();

// Returns a |ProofVerifyContext| that must be used with the verifier
// returned by |ProofVerifierForTesting|.
std::unique_ptr<ProofVerifyContext> ProofVerifyContextForTesting();

// MockCommonCertSets returns a CommonCertSets that contains a single set with
// hash |hash|, consisting of the certificate |cert| at index |index|.
CommonCertSets* MockCommonCertSets(quiche::QuicheStringPiece cert,
                                   uint64_t hash,
                                   uint32_t index);

// Creates a minimal dummy reject message that will pass the client-config
// validation tests. This will include a server config, but no certs, proof
// source address token, or server nonce.
void FillInDummyReject(CryptoHandshakeMessage* rej);

// ParseTag returns a QuicTag from parsing |tagstr|. |tagstr| may either be
// in the format "EXMP" (i.e. ASCII format), or "#11223344" (an explicit hex
// format). It CHECK fails if there's a parse error.
QuicTag ParseTag(const char* tagstr);

// Message constructs a CHLO message from a provided vector of tag/value pairs.
// The first of each pair is the tag of a tag/value and is given as an argument
// to |ParseTag|. The second is the value of the tag/value pair and is either a
// hex dump, preceeded by a '#', or a raw value. If minimum_size_bytes is
// provided then the message will be padded to this minimum size.
//
//   CreateCHLO(
//       {{"NOCE", "#11223344"},
//        {"SNI", "www.example.com"}},
//       optional_minimum_size_bytes);
CryptoHandshakeMessage CreateCHLO(
    std::vector<std::pair<std::string, std::string>> tags_and_values);
CryptoHandshakeMessage CreateCHLO(
    std::vector<std::pair<std::string, std::string>> tags_and_values,
    int minimum_size_bytes);

// MovePackets parses crypto handshake messages from packet number
// |*inout_packet_index| through to the last packet (or until a packet fails
// to decrypt) and has |dest_stream| process them. |*inout_packet_index| is
// updated with an index one greater than the last packet processed.
void MovePackets(PacketSavingConnection* source_conn,
                 size_t* inout_packet_index,
                 QuicCryptoStream* dest_stream,
                 PacketSavingConnection* dest_conn,
                 Perspective dest_perspective);

// Return an inchoate CHLO with some basic tag value pairs.
CryptoHandshakeMessage GenerateDefaultInchoateCHLO(
    const QuicClock* clock,
    QuicTransportVersion version,
    QuicCryptoServerConfig* crypto_config);

// Takes a inchoate CHLO, returns a full CHLO in |out| which can pass
// |crypto_config|'s validation.
void GenerateFullCHLO(
    const CryptoHandshakeMessage& inchoate_chlo,
    QuicCryptoServerConfig* crypto_config,
    QuicSocketAddress server_addr,
    QuicSocketAddress client_addr,
    QuicTransportVersion transport_version,
    const QuicClock* clock,
    QuicReferenceCountedPointer<QuicSignedServerConfig> signed_config,
    QuicCompressedCertsCache* compressed_certs_cache,
    CryptoHandshakeMessage* out);

void CompareClientAndServerKeys(QuicCryptoClientStream* client,
                                QuicCryptoServerStreamBase* server);

// Return a CHLO nonce in hexadecimal.
std::string GenerateClientNonceHex(const QuicClock* clock,
                                   QuicCryptoServerConfig* crypto_config);

// Return a CHLO PUBS in hexadecimal.
std::string GenerateClientPublicValuesHex();

}  // namespace crypto_test_utils

}  // namespace test

}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_CRYPTO_TEST_UTILS_H_
