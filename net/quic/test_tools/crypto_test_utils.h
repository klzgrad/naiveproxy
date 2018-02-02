// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_TEST_TOOLS_CRYPTO_TEST_UTILS_H_
#define NET_QUIC_TEST_TOOLS_CRYPTO_TEST_UTILS_H_

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "base/macros.h"
#include "net/quic/core/crypto/crypto_framer.h"
#include "net/quic/core/quic_framer.h"
#include "net/quic/core/quic_packets.h"
#include "net/quic/platform/api/quic_string_piece.h"
#include "net/quic/test_tools/quic_test_utils.h"
#include "third_party/boringssl/src/include/openssl/evp.h"

namespace net {

class ChannelIDSource;
class CommonCertSets;
class ProofSource;
class ProofVerifier;
class ProofVerifyContext;
class QuicClock;
class QuicConfig;
class QuicCryptoClientStream;
class QuicCryptoServerConfig;
class QuicCryptoServerStream;
class QuicCryptoStream;
class QuicRandom;
class QuicServerId;

namespace test {

class PacketSavingConnection;

class TestChannelIDKey : public ChannelIDKey {
 public:
  explicit TestChannelIDKey(EVP_PKEY* ecdsa_key);
  ~TestChannelIDKey() override;

  // ChannelIDKey implementation.

  bool Sign(QuicStringPiece signed_data,
            std::string* out_signature) const override;

  std::string SerializeKey() const override;

 private:
  bssl::UniquePtr<EVP_PKEY> ecdsa_key_;
};

class TestChannelIDSource : public ChannelIDSource {
 public:
  ~TestChannelIDSource() override;

  // ChannelIDSource implementation.

  QuicAsyncStatus GetChannelIDKey(
      const std::string& hostname,
      std::unique_ptr<ChannelIDKey>* channel_id_key,
      ChannelIDSourceCallback* /*callback*/) override;

 private:
  static EVP_PKEY* HostnameToKey(const std::string& hostname);
};

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

// FakeServerOptions bundles together a number of options for configuring the
// server in HandshakeWithFakeServer.
struct FakeServerOptions {
  FakeServerOptions();
  ~FakeServerOptions();

  // The Token Binding params that the server supports and will negotiate.
  QuicTagVector token_binding_params;
};

// FakeClientOptions bundles together a number of options for configuring
// HandshakeWithFakeClient.
struct FakeClientOptions {
  FakeClientOptions();
  ~FakeClientOptions();

  // If channel_id_enabled is true then the client will attempt to send a
  // ChannelID.
  bool channel_id_enabled;

  // If channel_id_source_async is true then the client will use an async
  // ChannelIDSource for testing. Ignored if channel_id_enabled is false.
  bool channel_id_source_async;

  // The Token Binding params that the client supports and will negotiate.
  QuicTagVector token_binding_params;
};

// returns: the number of client hellos that the client sent.
int HandshakeWithFakeServer(QuicConfig* server_quic_config,
                            MockQuicConnectionHelper* helper,
                            MockAlarmFactory* alarm_factory,
                            PacketSavingConnection* client_conn,
                            QuicCryptoClientStream* client,
                            const FakeServerOptions& options);

// returns: the number of client hellos that the client sent.
int HandshakeWithFakeClient(MockQuicConnectionHelper* helper,
                            MockAlarmFactory* alarm_factory,
                            PacketSavingConnection* server_conn,
                            QuicCryptoServerStream* server,
                            const QuicServerId& server_id,
                            const FakeClientOptions& options);

// SetupCryptoServerConfigForTest configures |crypto_config|
// with sensible defaults for testing.
void SetupCryptoServerConfigForTest(const QuicClock* clock,
                                    QuicRandom* rand,
                                    QuicCryptoServerConfig* crypto_config,
                                    const FakeServerOptions& options);

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

// CommunicateHandshakeMessagesAndRunCallbacks moves messages from |client|
// to |server| and back until |client|'s handshake has completed. If
// |callback_source| is not nullptr,
// CommunicateHandshakeMessagesAndRunCallbacks also runs callbacks from
// |callback_source| between processing messages.
void CommunicateHandshakeMessagesAndRunCallbacks(
    PacketSavingConnection* client_conn,
    QuicCryptoStream* client,
    PacketSavingConnection* server_conn,
    QuicCryptoStream* server,
    CallbackSource* callback_source);

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
ProofVerifyContext* ProofVerifyContextForTesting();

// MockCommonCertSets returns a CommonCertSets that contains a single set with
// hash |hash|, consisting of the certificate |cert| at index |index|.
CommonCertSets* MockCommonCertSets(QuicStringPiece cert,
                                   uint64_t hash,
                                   uint32_t index);

// Creates a minimal dummy reject message that will pass the client-config
// validation tests. This will include a server config, but no certs, proof
// source address token, or server nonce.
void FillInDummyReject(CryptoHandshakeMessage* rej, bool reject_is_stateless);

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

// ChannelIDSourceForTesting returns a ChannelIDSource that generates keys
// deterministically based on the hostname given in the GetChannelIDKey call.
// This ChannelIDSource works in synchronous mode, i.e., its GetChannelIDKey
// method never returns QUIC_PENDING.
ChannelIDSource* ChannelIDSourceForTesting();

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
    QuicTransportVersion version,
    const QuicClock* clock,
    QuicReferenceCountedPointer<QuicSignedServerConfig> signed_config,
    QuicCompressedCertsCache* compressed_certs_cache,
    CryptoHandshakeMessage* out);

void CompareClientAndServerKeys(QuicCryptoClientStream* client,
                                QuicCryptoServerStream* server);

// Return a CHLO nonce in hexadecimal.
std::string GenerateClientNonceHex(const QuicClock* clock,
                                   QuicCryptoServerConfig* crypto_config);

// Return a CHLO PUBS in hexadecimal.
std::string GenerateClientPublicValuesHex();

}  // namespace crypto_test_utils

}  // namespace test

}  // namespace net

#endif  // NET_QUIC_TEST_TOOLS_CRYPTO_TEST_UTILS_H_
