// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_QUIC_CRYPTO_CLIENT_CONFIG_H_
#define QUICHE_QUIC_CORE_CRYPTO_QUIC_CRYPTO_CLIENT_CONFIG_H_

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "third_party/boringssl/src/include/openssl/base.h"
#include "third_party/boringssl/src/include/openssl/ssl.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_handshake.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_protocol.h"
#include "net/third_party/quiche/src/quic/core/crypto/proof_source.h"
#include "net/third_party/quiche/src/quic/core/crypto/transport_parameters.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/core/quic_server_id.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_reference_counted.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

class CryptoHandshakeMessage;
class ProofVerifier;
class ProofVerifyDetails;
class QuicRandom;

// QuicResumptionState stores the state a client needs for performing connection
// resumption.
struct QUIC_EXPORT_PRIVATE QuicResumptionState {
  // |tls_session| holds the cryptographic state necessary for a resumption. It
  // includes the ALPN negotiated on the connection where the ticket was
  // received.
  bssl::UniquePtr<SSL_SESSION> tls_session;

  // If the application using QUIC doesn't support 0-RTT handshakes or the
  // client didn't receive a 0-RTT capable session ticket from the server,
  // |transport_params| will be null. Otherwise, it will contain the transport
  // parameters received from the server on the original connection.
  std::unique_ptr<TransportParameters> transport_params;

  // If |transport_params| is null, then |application_state| is ignored and
  // should be empty. |application_state| contains serialized state that the
  // client received from the server at the application layer that the client
  // needs to remember when performing a 0-RTT handshake.
  std::vector<uint8_t> application_state;
};

// SessionCache is an interface for managing storing and retrieving
// QuicResumptionState structs.
class QUIC_EXPORT_PRIVATE SessionCache {
 public:
  virtual ~SessionCache() {}

  // Inserts |state| into the cache, keyed by |server_id|. Insert is called
  // after a session ticket is received. If the session ticket is valid for
  // 0-RTT, there may be a delay between its receipt and the call to Insert
  // while waiting for application state for |state|.
  //
  // Insert may be called multiple times per connection. SessionCache
  // implementations should support storing multiple entries per server ID.
  virtual void Insert(const QuicServerId& server_id,
                      std::unique_ptr<QuicResumptionState> state) = 0;

  // Lookup is called once at the beginning of each TLS handshake to potentially
  // provide the saved state both for the TLS handshake and for sending 0-RTT
  // data (if supported). Lookup may return a nullptr. Implementations should
  // delete cache entries after returning them in Lookup so that session tickets
  // are used only once.
  virtual std::unique_ptr<QuicResumptionState> Lookup(
      const QuicServerId& server_id,
      const SSL_CTX* ctx) = 0;
};

// QuicCryptoClientConfig contains crypto-related configuration settings for a
// client. Note that this object isn't thread-safe. It's designed to be used on
// a single thread at a time.
class QUIC_EXPORT_PRIVATE QuicCryptoClientConfig : public QuicCryptoConfig {
 public:
  // A CachedState contains the information that the client needs in order to
  // perform a 0-RTT handshake with a server. This information can be reused
  // over several connections to the same server.
  class QUIC_EXPORT_PRIVATE CachedState {
   public:
    // Enum to track if the server config is valid or not. If it is not valid,
    // it specifies why it is invalid.
    enum ServerConfigState {
      // WARNING: Do not change the numerical values of any of server config
      // state. Do not remove deprecated server config states - just comment
      // them as deprecated.
      SERVER_CONFIG_EMPTY = 0,
      SERVER_CONFIG_INVALID = 1,
      SERVER_CONFIG_CORRUPTED = 2,
      SERVER_CONFIG_EXPIRED = 3,
      SERVER_CONFIG_INVALID_EXPIRY = 4,
      SERVER_CONFIG_VALID = 5,
      // NOTE: Add new server config states only immediately above this line.
      // Make sure to update the QuicServerConfigState enum in
      // tools/metrics/histograms/histograms.xml accordingly.
      SERVER_CONFIG_COUNT
    };

    CachedState();
    CachedState(const CachedState&) = delete;
    CachedState& operator=(const CachedState&) = delete;
    ~CachedState();

    // IsComplete returns true if this object contains enough information to
    // perform a handshake with the server. |now| is used to judge whether any
    // cached server config has expired.
    bool IsComplete(QuicWallTime now) const;

    // IsEmpty returns true if |server_config_| is empty.
    bool IsEmpty() const;

    // GetServerConfig returns the parsed contents of |server_config|, or
    // nullptr if |server_config| is empty. The return value is owned by this
    // object and is destroyed when this object is.
    const CryptoHandshakeMessage* GetServerConfig() const;

    // SetServerConfig checks that |server_config| parses correctly and stores
    // it in |server_config_|. |now| is used to judge whether |server_config|
    // has expired.
    ServerConfigState SetServerConfig(quiche::QuicheStringPiece server_config,
                                      QuicWallTime now,
                                      QuicWallTime expiry_time,
                                      std::string* error_details);

    // InvalidateServerConfig clears the cached server config (if any).
    void InvalidateServerConfig();

    // SetProof stores a cert chain, cert signed timestamp and signature.
    void SetProof(const std::vector<std::string>& certs,
                  quiche::QuicheStringPiece cert_sct,
                  quiche::QuicheStringPiece chlo_hash,
                  quiche::QuicheStringPiece signature);

    // Clears all the data.
    void Clear();

    // Clears the certificate chain and signature and invalidates the proof.
    void ClearProof();

    // SetProofValid records that the certificate chain and signature have been
    // validated and that it's safe to assume that the server is legitimate.
    // (Note: this does not check the chain or signature.)
    void SetProofValid();

    // If the server config or the proof has changed then it needs to be
    // revalidated. Helper function to keep server_config_valid_ and
    // generation_counter_ in sync.
    void SetProofInvalid();

    const std::string& server_config() const;
    const std::string& source_address_token() const;
    const std::vector<std::string>& certs() const;
    const std::string& cert_sct() const;
    const std::string& chlo_hash() const;
    const std::string& signature() const;
    bool proof_valid() const;
    uint64_t generation_counter() const;
    const ProofVerifyDetails* proof_verify_details() const;

    void set_source_address_token(quiche::QuicheStringPiece token);

    void set_cert_sct(quiche::QuicheStringPiece cert_sct);

    // Adds the connection ID to the queue of server-designated connection-ids.
    void add_server_designated_connection_id(QuicConnectionId connection_id);

    // If true, the crypto config contains at least one connection ID specified
    // by the server, and the client should use one of these IDs when initiating
    // the next connection.
    bool has_server_designated_connection_id() const;

    // This function should only be called when
    // has_server_designated_connection_id is true.  Returns the next
    // connection_id specified by the server and removes it from the
    // queue of ids.
    QuicConnectionId GetNextServerDesignatedConnectionId();

    // Adds the servernonce to the queue of server nonces.
    void add_server_nonce(const std::string& server_nonce);

    // If true, the crypto config contains at least one server nonce, and the
    // client should use one of these nonces.
    bool has_server_nonce() const;

    // This function should only be called when has_server_nonce is true.
    // Returns the next server_nonce specified by the server and removes it
    // from the queue of nonces.
    std::string GetNextServerNonce();

    // SetProofVerifyDetails takes ownership of |details|.
    void SetProofVerifyDetails(ProofVerifyDetails* details);

    // Copy the |server_config_|, |source_address_token_|, |certs_|,
    // |expiration_time_|, |cert_sct_|, |chlo_hash_| and |server_config_sig_|
    // from the |other|.  The remaining fields, |generation_counter_|,
    // |proof_verify_details_|, and |scfg_| remain unchanged.
    void InitializeFrom(const CachedState& other);

    // Initializes this cached state based on the arguments provided.
    // Returns false if there is a problem parsing the server config.
    bool Initialize(quiche::QuicheStringPiece server_config,
                    quiche::QuicheStringPiece source_address_token,
                    const std::vector<std::string>& certs,
                    const std::string& cert_sct,
                    quiche::QuicheStringPiece chlo_hash,
                    quiche::QuicheStringPiece signature,
                    QuicWallTime now,
                    QuicWallTime expiration_time);

   private:
    std::string server_config_;         // A serialized handshake message.
    std::string source_address_token_;  // An opaque proof of IP ownership.
    std::vector<std::string> certs_;    // A list of certificates in leaf-first
                                        // order.
    std::string cert_sct_;              // Signed timestamp of the leaf cert.
    std::string chlo_hash_;             // Hash of the CHLO message.
    std::string server_config_sig_;     // A signature of |server_config_|.
    bool server_config_valid_;          // True if |server_config_| is correctly
                                // signed and |certs_| has been validated.
    QuicWallTime expiration_time_;  // Time when the config is no longer valid.
    // Generation counter associated with the |server_config_|, |certs_| and
    // |server_config_sig_| combination. It is incremented whenever we set
    // server_config_valid_ to false.
    uint64_t generation_counter_;

    std::unique_ptr<ProofVerifyDetails> proof_verify_details_;

    // scfg contains the cached, parsed value of |server_config|.
    mutable std::unique_ptr<CryptoHandshakeMessage> scfg_;

    // TODO(jokulik): Consider using a hash-set as extra book-keeping to ensure
    // that no connection-id is added twice.  Also, consider keeping the server
    // nonces and connection_ids together in one queue.
    QuicQueue<QuicConnectionId> server_designated_connection_ids_;
    QuicQueue<std::string> server_nonces_;
  };

  // Used to filter server ids for partial config deletion.
  class QUIC_EXPORT_PRIVATE ServerIdFilter {
   public:
    virtual ~ServerIdFilter() {}

    // Returns true if |server_id| matches the filter.
    virtual bool Matches(const QuicServerId& server_id) const = 0;
  };

  // DEPRECATED: Use the constructor below instead.
  explicit QuicCryptoClientConfig(
      std::unique_ptr<ProofVerifier> proof_verifier);
  QuicCryptoClientConfig(std::unique_ptr<ProofVerifier> proof_verifier,
                         std::unique_ptr<SessionCache> session_cache);
  QuicCryptoClientConfig(const QuicCryptoClientConfig&) = delete;
  QuicCryptoClientConfig& operator=(const QuicCryptoClientConfig&) = delete;
  ~QuicCryptoClientConfig();

  // LookupOrCreate returns a CachedState for the given |server_id|. If no such
  // CachedState currently exists, it will be created and cached.
  CachedState* LookupOrCreate(const QuicServerId& server_id);

  // Delete CachedState objects whose server ids match |filter| from
  // cached_states.
  void ClearCachedStates(const ServerIdFilter& filter);

  // FillInchoateClientHello sets |out| to be a CHLO message that elicits a
  // source-address token or SCFG from a server. If |cached| is non-nullptr, the
  // source-address token will be taken from it. |out_params| is used in order
  // to store the cached certs that were sent as hints to the server in
  // |out_params->cached_certs|. |preferred_version| is the version of the
  // QUIC protocol that this client chose to use initially. This allows the
  // server to detect downgrade attacks.  If |demand_x509_proof| is true,
  // then |out| will include an X509 proof demand, and the associated
  // certificate related fields.
  void FillInchoateClientHello(
      const QuicServerId& server_id,
      const ParsedQuicVersion preferred_version,
      const CachedState* cached,
      QuicRandom* rand,
      bool demand_x509_proof,
      QuicReferenceCountedPointer<QuicCryptoNegotiatedParameters> out_params,
      CryptoHandshakeMessage* out) const;

  // FillClientHello sets |out| to be a CHLO message based on the configuration
  // of this object. This object must have cached enough information about
  // the server's hostname in order to perform a handshake. This can be checked
  // with the |IsComplete| member of |CachedState|.
  //
  // |now| and |rand| are used to generate the nonce and |out_params| is
  // filled with the results of the handshake that the server is expected to
  // accept. |preferred_version| is the version of the QUIC protocol that this
  // client chose to use initially. This allows the server to detect downgrade
  // attacks.
  //
  // If |channel_id_key| is not null, it is used to sign a secret value derived
  // from the client and server's keys, and the Channel ID public key and the
  // signature are placed in the CETV value of the CHLO.
  QuicErrorCode FillClientHello(
      const QuicServerId& server_id,
      QuicConnectionId connection_id,
      const ParsedQuicVersion preferred_version,
      const ParsedQuicVersion actual_version,
      const CachedState* cached,
      QuicWallTime now,
      QuicRandom* rand,
      QuicReferenceCountedPointer<QuicCryptoNegotiatedParameters> out_params,
      CryptoHandshakeMessage* out,
      std::string* error_details) const;

  // ProcessRejection processes a REJ message from a server and updates the
  // cached information about that server. After this, |IsComplete| may return
  // true for that server's CachedState. If the rejection message contains state
  // about a future handshake (i.e. an nonce value from the server), then it
  // will be saved in |out_params|. |now| is used to judge whether the server
  // config in the rejection message has expired.
  QuicErrorCode ProcessRejection(
      const CryptoHandshakeMessage& rej,
      QuicWallTime now,
      QuicTransportVersion version,
      quiche::QuicheStringPiece chlo_hash,
      CachedState* cached,
      QuicReferenceCountedPointer<QuicCryptoNegotiatedParameters> out_params,
      std::string* error_details);

  // ProcessServerHello processes the message in |server_hello|, updates the
  // cached information about that server, writes the negotiated parameters to
  // |out_params| and returns QUIC_NO_ERROR. If |server_hello| is unacceptable
  // then it puts an error message in |error_details| and returns an error
  // code. |version| is the QUIC version for the current connection.
  // |negotiated_versions| contains the list of version, if any, that were
  // present in a version negotiation packet previously received from the
  // server. The contents of this list will be compared against the list of
  // versions provided in the VER tag of the server hello.
  QuicErrorCode ProcessServerHello(
      const CryptoHandshakeMessage& server_hello,
      QuicConnectionId connection_id,
      ParsedQuicVersion version,
      const ParsedQuicVersionVector& negotiated_versions,
      CachedState* cached,
      QuicReferenceCountedPointer<QuicCryptoNegotiatedParameters> out_params,
      std::string* error_details);

  // Processes the message in |server_update|, updating the cached source
  // address token, and server config.
  // If |server_update| is invalid then |error_details| will contain an error
  // message, and an error code will be returned. If all has gone well
  // QUIC_NO_ERROR is returned.
  QuicErrorCode ProcessServerConfigUpdate(
      const CryptoHandshakeMessage& server_update,
      QuicWallTime now,
      const QuicTransportVersion version,
      quiche::QuicheStringPiece chlo_hash,
      CachedState* cached,
      QuicReferenceCountedPointer<QuicCryptoNegotiatedParameters> out_params,
      std::string* error_details);

  ProofVerifier* proof_verifier() const;
  SessionCache* session_cache() const;
  ProofSource* proof_source() const;
  void set_proof_source(std::unique_ptr<ProofSource> proof_source);
  SSL_CTX* ssl_ctx() const;

  // Initialize the CachedState from |canonical_crypto_config| for the
  // |canonical_server_id| as the initial CachedState for |server_id|. We will
  // copy config data only if |canonical_crypto_config| has valid proof.
  void InitializeFrom(const QuicServerId& server_id,
                      const QuicServerId& canonical_server_id,
                      QuicCryptoClientConfig* canonical_crypto_config);

  // Adds |suffix| as a domain suffix for which the server's crypto config
  // is expected to be shared among servers with the domain suffix. If a server
  // matches this suffix, then the server config from another server with the
  // suffix will be used to initialize the cached state for this server.
  void AddCanonicalSuffix(const std::string& suffix);

  // Saves the |user_agent_id| that will be passed in QUIC's CHLO message.
  void set_user_agent_id(const std::string& user_agent_id) {
    user_agent_id_ = user_agent_id;
  }

  // Returns the user_agent_id that will be provided in the client hello
  // handshake message.
  const std::string& user_agent_id() const { return user_agent_id_; }

  // Saves the |alpn| that will be passed in QUIC's CHLO message.
  void set_alpn(const std::string& alpn) { alpn_ = alpn; }

  void set_pre_shared_key(quiche::QuicheStringPiece psk) {
    pre_shared_key_ = std::string(psk);
  }

  bool pad_inchoate_hello() const { return pad_inchoate_hello_; }
  void set_pad_inchoate_hello(bool new_value) {
    pad_inchoate_hello_ = new_value;
  }

  bool pad_full_hello() const { return pad_full_hello_; }
  void set_pad_full_hello(bool new_value) { pad_full_hello_ = new_value; }

 private:
  // Sets the members to reasonable, default values.
  void SetDefaults();

  // CacheNewServerConfig checks for SCFG, STK, PROF, and CRT tags in |message|,
  // verifies them, and stores them in the cached state if they validate.
  // This is used on receipt of a REJ from a server, or when a server sends
  // updated server config during a connection.
  QuicErrorCode CacheNewServerConfig(
      const CryptoHandshakeMessage& message,
      QuicWallTime now,
      QuicTransportVersion version,
      quiche::QuicheStringPiece chlo_hash,
      const std::vector<std::string>& cached_certs,
      CachedState* cached,
      std::string* error_details);

  // If the suffix of the hostname in |server_id| is in |canonical_suffixes_|,
  // then populate |cached| with the canonical cached state from
  // |canonical_server_map_| for that suffix. Returns true if |cached| is
  // initialized with canonical cached state.
  bool PopulateFromCanonicalConfig(const QuicServerId& server_id,
                                   CachedState* cached);

  // cached_states_ maps from the server_id to the cached information about
  // that server.
  std::map<QuicServerId, std::unique_ptr<CachedState>> cached_states_;

  // Contains a map of servers which could share the same server config. Map
  // from a canonical host suffix/port/scheme to a representative server with
  // the canonical suffix, which has a plausible set of initial certificates
  // (or at least server public key).
  std::map<QuicServerId, QuicServerId> canonical_server_map_;

  // Contains list of suffixes (for exmaple ".c.youtube.com",
  // ".googlevideo.com") of canonical hostnames.
  std::vector<std::string> canonical_suffixes_;

  std::unique_ptr<ProofVerifier> proof_verifier_;
  std::unique_ptr<SessionCache> session_cache_;
  std::unique_ptr<ProofSource> proof_source_;
  bssl::UniquePtr<SSL_CTX> ssl_ctx_;

  // The |user_agent_id_| passed in QUIC's CHLO message.
  std::string user_agent_id_;

  // The |alpn_| passed in QUIC's CHLO message.
  std::string alpn_;

  // If non-empty, the client will operate in the pre-shared key mode by
  // incorporating |pre_shared_key_| into the key schedule.
  std::string pre_shared_key_;

  // In QUIC, technically, client hello should be fully padded.
  // However, fully padding on slow network connection (e.g. 50kbps) can add
  // 150ms latency to one roundtrip. Therefore, you can disable padding of
  // individual messages. It is recommend to leave at least one message in
  // each direction fully padded (e.g. full CHLO and SHLO), but if you know
  // the lower-bound MTU, you don't need to pad all of them (keep in mind that
  // it's not OK to do it according to the standard).
  //
  // Also, if you disable padding, you must disable (change) the
  // anti-amplification protection. You should only do so if you have some
  // other means of verifying the client.
  bool pad_inchoate_hello_ = true;
  bool pad_full_hello_ = true;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_QUIC_CRYPTO_CLIENT_CONFIG_H_
