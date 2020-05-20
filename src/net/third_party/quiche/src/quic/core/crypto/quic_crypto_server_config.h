// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_QUIC_CRYPTO_SERVER_CONFIG_H_
#define QUICHE_QUIC_CORE_CRYPTO_QUIC_CRYPTO_SERVER_CONFIG_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "third_party/boringssl/src/include/openssl/base.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_handshake.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_handshake_message.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_protocol.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_secret_boxer.h"
#include "net/third_party/quiche/src/quic/core/crypto/key_exchange.h"
#include "net/third_party/quiche/src/quic/core/crypto/proof_source.h"
#include "net/third_party/quiche/src/quic/core/crypto/proof_verifier.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_compressed_certs_cache.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_crypto_proof.h"
#include "net/third_party/quiche/src/quic/core/crypto/server_proof_verifier.h"
#include "net/third_party/quiche/src/quic/core/proto/cached_network_parameters_proto.h"
#include "net/third_party/quiche/src/quic/core/proto/source_address_token_proto.h"
#include "net/third_party/quiche/src/quic/core/quic_time.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_mutex.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_reference_counted.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_socket_address.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

class CryptoHandshakeMessage;
class ProofSource;
class QuicClock;
class QuicRandom;
class QuicServerConfigProtobuf;
struct QuicSignedServerConfig;

// ClientHelloInfo contains information about a client hello message that is
// only kept for as long as it's being processed.
struct QUIC_EXPORT_PRIVATE ClientHelloInfo {
  ClientHelloInfo(const QuicIpAddress& in_client_ip, QuicWallTime in_now);
  ClientHelloInfo(const ClientHelloInfo& other);
  ~ClientHelloInfo();

  // Inputs to EvaluateClientHello.
  const QuicIpAddress client_ip;
  const QuicWallTime now;

  // Outputs from EvaluateClientHello.
  bool valid_source_address_token;
  quiche::QuicheStringPiece sni;
  quiche::QuicheStringPiece client_nonce;
  quiche::QuicheStringPiece server_nonce;
  quiche::QuicheStringPiece user_agent_id;
  SourceAddressTokens source_address_tokens;

  // Errors from EvaluateClientHello.
  std::vector<uint32_t> reject_reasons;
  static_assert(sizeof(QuicTag) == sizeof(uint32_t), "header out of sync");
};

namespace test {
class QuicCryptoServerConfigPeer;
}  // namespace test

// Hook that allows application code to subscribe to primary config changes.
class QUIC_EXPORT_PRIVATE PrimaryConfigChangedCallback {
 public:
  PrimaryConfigChangedCallback();
  PrimaryConfigChangedCallback(const PrimaryConfigChangedCallback&) = delete;
  PrimaryConfigChangedCallback& operator=(const PrimaryConfigChangedCallback&) =
      delete;
  virtual ~PrimaryConfigChangedCallback();
  virtual void Run(const std::string& scid) = 0;
};

// Callback used to accept the result of the |client_hello| validation step.
class QUIC_EXPORT_PRIVATE ValidateClientHelloResultCallback {
 public:
  // Opaque token that holds information about the client_hello and
  // its validity.  Can be interpreted by calling ProcessClientHello.
  struct QUIC_EXPORT_PRIVATE Result : public QuicReferenceCounted {
    Result(const CryptoHandshakeMessage& in_client_hello,
           QuicIpAddress in_client_ip,
           QuicWallTime in_now);

    CryptoHandshakeMessage client_hello;
    ClientHelloInfo info;
    QuicErrorCode error_code;
    std::string error_details;

    // Populated if the CHLO STK contained a CachedNetworkParameters proto.
    CachedNetworkParameters cached_network_params;

   protected:
    ~Result() override;
  };

  ValidateClientHelloResultCallback();
  ValidateClientHelloResultCallback(const ValidateClientHelloResultCallback&) =
      delete;
  ValidateClientHelloResultCallback& operator=(
      const ValidateClientHelloResultCallback&) = delete;
  virtual ~ValidateClientHelloResultCallback();
  virtual void Run(QuicReferenceCountedPointer<Result> result,
                   std::unique_ptr<ProofSource::Details> details) = 0;
};

// Callback used to accept the result of the ProcessClientHello method.
class QUIC_EXPORT_PRIVATE ProcessClientHelloResultCallback {
 public:
  ProcessClientHelloResultCallback();
  ProcessClientHelloResultCallback(const ProcessClientHelloResultCallback&) =
      delete;
  ProcessClientHelloResultCallback& operator=(
      const ProcessClientHelloResultCallback&) = delete;
  virtual ~ProcessClientHelloResultCallback();
  virtual void Run(QuicErrorCode error,
                   const std::string& error_details,
                   std::unique_ptr<CryptoHandshakeMessage> message,
                   std::unique_ptr<DiversificationNonce> diversification_nonce,
                   std::unique_ptr<ProofSource::Details> details) = 0;
};

// Callback used to receive the results of a call to
// BuildServerConfigUpdateMessage.
class QUIC_EXPORT_PRIVATE BuildServerConfigUpdateMessageResultCallback {
 public:
  BuildServerConfigUpdateMessageResultCallback() = default;
  virtual ~BuildServerConfigUpdateMessageResultCallback() {}
  BuildServerConfigUpdateMessageResultCallback(
      const BuildServerConfigUpdateMessageResultCallback&) = delete;
  BuildServerConfigUpdateMessageResultCallback& operator=(
      const BuildServerConfigUpdateMessageResultCallback&) = delete;
  virtual void Run(bool ok, const CryptoHandshakeMessage& message) = 0;
};

// Object that is interested in built rejections (which include REJ, SREJ and
// cheap SREJ).
class QUIC_EXPORT_PRIVATE RejectionObserver {
 public:
  RejectionObserver() = default;
  virtual ~RejectionObserver() {}
  RejectionObserver(const RejectionObserver&) = delete;
  RejectionObserver& operator=(const RejectionObserver&) = delete;
  // Called after a rejection is built.
  virtual void OnRejectionBuilt(const std::vector<uint32_t>& reasons,
                                CryptoHandshakeMessage* out) const = 0;
};

// Factory for creating KeyExchange objects.
class QUIC_EXPORT_PRIVATE KeyExchangeSource {
 public:
  virtual ~KeyExchangeSource() = default;

  // Returns the default KeyExchangeSource.
  static std::unique_ptr<KeyExchangeSource> Default();

  // Create a new KeyExchange using the curve specified by |type| using the
  // specified private key.  |private_key| may be empty for key-exchange
  // mechanisms which do not hold the private key in-process.  If |is_fallback|
  // is set, |private_key| is required to be set, and a local key-exchange
  // object should be returned.
  virtual std::unique_ptr<AsynchronousKeyExchange> Create(
      std::string server_config_id,
      bool is_fallback,
      QuicTag type,
      quiche::QuicheStringPiece private_key) = 0;
};

// QuicCryptoServerConfig contains the crypto configuration of a QUIC server.
// Unlike a client, a QUIC server can have multiple configurations active in
// order to support clients resuming with a previous configuration.
// TODO(agl): when adding configurations at runtime is added, this object will
// need to consider locking.
class QUIC_EXPORT_PRIVATE QuicCryptoServerConfig {
 public:
  // ConfigOptions contains options for generating server configs.
  struct QUIC_EXPORT_PRIVATE ConfigOptions {
    ConfigOptions();
    ConfigOptions(const ConfigOptions& other);
    ~ConfigOptions();

    // expiry_time is the time, in UNIX seconds, when the server config will
    // expire. If unset, it defaults to the current time plus six months.
    QuicWallTime expiry_time;
    // channel_id_enabled controls whether the server config will indicate
    // support for ChannelIDs.
    bool channel_id_enabled;
    // id contains the server config id for the resulting config. If empty, a
    // random id is generated.
    std::string id;
    // orbit contains the kOrbitSize bytes of the orbit value for the server
    // config. If |orbit| is empty then a random orbit is generated.
    std::string orbit;
    // p256 determines whether a P-256 public key will be included in the
    // server config. Note that this breaks deterministic server-config
    // generation since P-256 key generation doesn't use the QuicRandom given
    // to DefaultConfig().
    bool p256;
  };

  // |source_address_token_secret|: secret key material used for encrypting and
  //     decrypting source address tokens. It can be of any length as it is fed
  //     into a KDF before use. In tests, use TESTING.
  // |server_nonce_entropy|: an entropy source used to generate the orbit and
  //     key for server nonces, which are always local to a given instance of a
  //     server. Not owned.
  // |proof_source|: provides certificate chains and signatures.
  // |key_exchange_source|: provides key-exchange functionality.
  QuicCryptoServerConfig(
      quiche::QuicheStringPiece source_address_token_secret,
      QuicRandom* server_nonce_entropy,
      std::unique_ptr<ProofSource> proof_source,
      std::unique_ptr<KeyExchangeSource> key_exchange_source);
  QuicCryptoServerConfig(const QuicCryptoServerConfig&) = delete;
  QuicCryptoServerConfig& operator=(const QuicCryptoServerConfig&) = delete;
  ~QuicCryptoServerConfig();

  // TESTING is a magic parameter for passing to the constructor in tests.
  static const char TESTING[];

  // Generates a QuicServerConfigProtobuf protobuf suitable for
  // AddConfig and SetConfigs.
  static QuicServerConfigProtobuf GenerateConfig(QuicRandom* rand,
                                                 const QuicClock* clock,
                                                 const ConfigOptions& options);

  // AddConfig adds a QuicServerConfigProtobuf to the available configurations.
  // It returns the SCFG message from the config if successful. |now| is used in
  // conjunction with |protobuf->primary_time()| to determine whether the
  // config should be made primary.
  std::unique_ptr<CryptoHandshakeMessage> AddConfig(
      const QuicServerConfigProtobuf& protobuf,
      QuicWallTime now);

  // AddDefaultConfig calls DefaultConfig to create a config and then calls
  // AddConfig to add it. See the comment for |DefaultConfig| for details of
  // the arguments.
  std::unique_ptr<CryptoHandshakeMessage> AddDefaultConfig(
      QuicRandom* rand,
      const QuicClock* clock,
      const ConfigOptions& options);

  // SetConfigs takes a vector of config protobufs and the current time.
  // Configs are assumed to be uniquely identified by their server config ID.
  // Previously unknown configs are added and possibly made the primary config
  // depending on their |primary_time| and the value of |now|. Configs that are
  // known, but are missing from the protobufs are deleted, unless they are
  // currently the primary config. SetConfigs returns false if any errors were
  // encountered and no changes to the QuicCryptoServerConfig will occur.
  bool SetConfigs(const std::vector<QuicServerConfigProtobuf>& protobufs,
                  const QuicServerConfigProtobuf* fallback_protobuf,
                  QuicWallTime now);

  // SetSourceAddressTokenKeys sets the keys to be tried, in order, when
  // decrypting a source address token.  Note that these keys are used *without*
  // passing them through a KDF, in contradistinction to the
  // |source_address_token_secret| argument to the constructor.
  void SetSourceAddressTokenKeys(const std::vector<std::string>& keys);

  // Get the server config ids for all known configs.
  void GetConfigIds(std::vector<std::string>* scids) const;

  // Checks |client_hello| for gross errors and determines whether it can be
  // shown to be fresh (i.e. not a replay).  The result of the validation step
  // must be interpreted by calling QuicCryptoServerConfig::ProcessClientHello
  // from the done_cb.
  //
  // ValidateClientHello may invoke the done_cb before unrolling the
  // stack if it is able to assess the validity of the client_nonce
  // without asynchronous operations.
  //
  // client_hello: the incoming client hello message.
  // client_ip: the IP address of the client, which is used to generate and
  //     validate source-address tokens.
  // server_address: the IP address and port of the server. The IP address and
  //     port may be used for certificate selection.
  // version: protocol version used for this connection.
  // clock: used to validate client nonces and ephemeral keys.
  // crypto_proof: in/out parameter to which will be written the crypto proof
  //               used in reply to a proof demand.  The pointed-to-object must
  //               live until the callback is invoked.
  // done_cb: single-use callback that accepts an opaque
  //     ValidatedClientHelloMsg token that holds information about
  //     the client hello.  The callback will always be called exactly
  //     once, either under the current call stack, or after the
  //     completion of an asynchronous operation.
  void ValidateClientHello(
      const CryptoHandshakeMessage& client_hello,
      const QuicIpAddress& client_ip,
      const QuicSocketAddress& server_address,
      QuicTransportVersion version,
      const QuicClock* clock,
      QuicReferenceCountedPointer<QuicSignedServerConfig> crypto_proof,
      std::unique_ptr<ValidateClientHelloResultCallback> done_cb) const;

  // ProcessClientHello processes |client_hello| and decides whether to accept
  // or reject the connection. If the connection is to be accepted, |done_cb| is
  // invoked with the contents of the ServerHello and QUIC_NO_ERROR. Otherwise
  // |done_cb| is called with a REJ or SREJ message and QUIC_NO_ERROR.
  //
  // validate_chlo_result: Output from the asynchronous call to
  //     ValidateClientHello.  Contains the client hello message and
  //     information about it.
  // reject_only: Only generate rejections, not server hello messages.
  // connection_id: the ConnectionId for the connection, which is used in key
  //     derivation.
  // server_ip: the IP address of the server. The IP address may be used for
  //     certificate selection.
  // client_address: the IP address and port of the client. The IP address is
  //     used to generate and validate source-address tokens.
  // version: version of the QUIC protocol in use for this connection
  // supported_versions: versions of the QUIC protocol that this server
  //     supports.
  // clock: used to validate client nonces and ephemeral keys.
  // rand: an entropy source
  // compressed_certs_cache: the cache that caches a set of most recently used
  //     certs. Owned by QuicDispatcher.
  // params: the state of the handshake. This may be updated with a server
  //     nonce when we send a rejection.
  // crypto_proof: output structure containing the crypto proof used in reply to
  //     a proof demand.
  // total_framing_overhead: the total per-packet overhead for a stream frame
  // chlo_packet_size: the size, in bytes, of the CHLO packet
  // done_cb: the callback invoked on completion
  void ProcessClientHello(
      QuicReferenceCountedPointer<ValidateClientHelloResultCallback::Result>
          validate_chlo_result,
      bool reject_only,
      QuicConnectionId connection_id,
      const QuicSocketAddress& server_address,
      const QuicSocketAddress& client_address,
      ParsedQuicVersion version,
      const ParsedQuicVersionVector& supported_versions,
      const QuicClock* clock,
      QuicRandom* rand,
      QuicCompressedCertsCache* compressed_certs_cache,
      QuicReferenceCountedPointer<QuicCryptoNegotiatedParameters> params,
      QuicReferenceCountedPointer<QuicSignedServerConfig> crypto_proof,
      QuicByteCount total_framing_overhead,
      QuicByteCount chlo_packet_size,
      std::unique_ptr<ProcessClientHelloResultCallback> done_cb) const;

  // BuildServerConfigUpdateMessage invokes |cb| with a SCUP message containing
  // the current primary config, an up to date source-address token, and cert
  // chain and proof in the case of secure QUIC. Passes true to |cb| if the
  // message was generated successfully, and false otherwise.  This method
  // assumes ownership of |cb|.
  //
  // |cached_network_params| is optional, and can be nullptr.
  void BuildServerConfigUpdateMessage(
      QuicTransportVersion version,
      quiche::QuicheStringPiece chlo_hash,
      const SourceAddressTokens& previous_source_address_tokens,
      const QuicSocketAddress& server_address,
      const QuicIpAddress& client_ip,
      const QuicClock* clock,
      QuicRandom* rand,
      QuicCompressedCertsCache* compressed_certs_cache,
      const QuicCryptoNegotiatedParameters& params,
      const CachedNetworkParameters* cached_network_params,
      std::unique_ptr<BuildServerConfigUpdateMessageResultCallback> cb) const;

  // set_replay_protection controls whether replay protection is enabled. If
  // replay protection is disabled then no strike registers are needed and
  // frontends can share an orbit value without a shared strike-register.
  // However, an attacker can duplicate a handshake and cause a client's
  // request to be processed twice.
  void set_replay_protection(bool on);

  // set_chlo_multiplier specifies the multiple of the CHLO message size
  // that a REJ message must stay under when the client doesn't present a
  // valid source-address token.
  void set_chlo_multiplier(size_t multiplier);

  // When sender is allowed to not pad client hello (not standards compliant),
  // we need to disable the client hello check.
  void set_validate_chlo_size(bool new_value) {
    validate_chlo_size_ = new_value;
  }

  // Returns whether the sender is allowed to not pad the client hello.
  bool validate_chlo_size() const { return validate_chlo_size_; }

  // When QUIC is tunneled through some other mechanism, source token validation
  // may be disabled. Do not disable it if you are not providing other
  // protection. (|true| protects against UDP amplification attack.).
  void set_validate_source_address_token(bool new_value) {
    validate_source_address_token_ = new_value;
  }

  // set_source_address_token_future_secs sets the number of seconds into the
  // future that source-address tokens will be accepted from. Since
  // source-address tokens are authenticated, this should only happen if
  // another, valid server has clock-skew.
  void set_source_address_token_future_secs(uint32_t future_secs);

  // set_source_address_token_lifetime_secs sets the number of seconds that a
  // source-address token will be valid for.
  void set_source_address_token_lifetime_secs(uint32_t lifetime_secs);

  // set_enable_serving_sct enables or disables serving signed cert timestamp
  // (RFC6962) in server hello.
  void set_enable_serving_sct(bool enable_serving_sct);

  // Set and take ownership of the callback to invoke on primary config changes.
  void AcquirePrimaryConfigChangedCb(
      std::unique_ptr<PrimaryConfigChangedCallback> cb);

  // Returns the number of configs this object owns.
  int NumberOfConfigs() const;

  // Callers retain the ownership of |rejection_observer| which must outlive the
  // config.
  void set_rejection_observer(RejectionObserver* rejection_observer) {
    rejection_observer_ = rejection_observer;
  }

  ProofSource* proof_source() const;
  ServerProofVerifier* proof_verifier() const;
  void set_proof_verifier(std::unique_ptr<ServerProofVerifier> proof_verifier);

  ClientCertMode client_cert_mode() const;
  void set_client_cert_mode(ClientCertMode client_cert_mode);

  SSL_CTX* ssl_ctx() const;

  void set_pre_shared_key(quiche::QuicheStringPiece psk) {
    pre_shared_key_ = std::string(psk);
  }

  bool pad_rej() const { return pad_rej_; }
  void set_pad_rej(bool new_value) { pad_rej_ = new_value; }

  bool pad_shlo() const { return pad_shlo_; }
  void set_pad_shlo(bool new_value) { pad_shlo_ = new_value; }

 private:
  friend class test::QuicCryptoServerConfigPeer;
  friend struct QuicSignedServerConfig;

  // Config represents a server config: a collection of preferences and
  // Diffie-Hellman public values.
  class QUIC_EXPORT_PRIVATE Config : public QuicCryptoConfig,
                                     public QuicReferenceCounted {
   public:
    Config();
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    // TODO(rtenneti): since this is a class, we should probably do
    // getters/setters here.
    // |serialized| contains the bytes of this server config, suitable for
    // sending on the wire.
    std::string serialized;
    // id contains the SCID of this server config.
    std::string id;
    // orbit contains the orbit value for this config: an opaque identifier
    // used to identify clusters of server frontends.
    unsigned char orbit[kOrbitSize];

    // key_exchanges contains key exchange objects. The values correspond,
    // one-to-one, with the tags in |kexs| from the parent class.
    std::vector<std::unique_ptr<AsynchronousKeyExchange>> key_exchanges;

    // tag_value_map contains the raw key/value pairs for the config.
    QuicTagValueMap tag_value_map;

    // channel_id_enabled is true if the config in |serialized| specifies that
    // ChannelIDs are supported.
    bool channel_id_enabled;

    // is_primary is true if this config is the one that we'll give out to
    // clients as the current one.
    bool is_primary;

    // primary_time contains the timestamp when this config should become the
    // primary config. A value of QuicWallTime::Zero() means that this config
    // will not be promoted at a specific time.
    QuicWallTime primary_time;

    // expiry_time contains the timestamp when this config expires.
    QuicWallTime expiry_time;

    // Secondary sort key for use when selecting primary configs and
    // there are multiple configs with the same primary time.
    // Smaller numbers mean higher priority.
    uint64_t priority;

    // source_address_token_boxer_ is used to protect the
    // source-address tokens that are given to clients.
    // Points to either source_address_token_boxer_storage or the
    // default boxer provided by QuicCryptoServerConfig.
    const CryptoSecretBoxer* source_address_token_boxer;

    // Holds the override source_address_token_boxer instance if the
    // Config is not using the default source address token boxer
    // instance provided by QuicCryptoServerConfig.
    std::unique_ptr<CryptoSecretBoxer> source_address_token_boxer_storage;

   private:
    ~Config() override;
  };

  typedef std::map<ServerConfigID, QuicReferenceCountedPointer<Config>>
      ConfigMap;

  // Get a ref to the config with a given server config id.
  QuicReferenceCountedPointer<Config> GetConfigWithScid(
      quiche::QuicheStringPiece requested_scid) const
      QUIC_SHARED_LOCKS_REQUIRED(configs_lock_);

  // A snapshot of the configs associated with an in-progress handshake.
  struct QUIC_EXPORT_PRIVATE Configs {
    QuicReferenceCountedPointer<Config> requested;
    QuicReferenceCountedPointer<Config> primary;
    QuicReferenceCountedPointer<Config> fallback;
  };

  // Get a snapshot of the current configs associated with a handshake.  If this
  // method was called earlier in this handshake |old_primary_config| should be
  // set to the primary config returned from that invocation, otherwise nullptr.
  //
  // Returns true if any configs are loaded.  If false is returned, |configs| is
  // not modified.
  bool GetCurrentConfigs(const QuicWallTime& now,
                         quiche::QuicheStringPiece requested_scid,
                         QuicReferenceCountedPointer<Config> old_primary_config,
                         Configs* configs) const;

  // ConfigPrimaryTimeLessThan returns true if a->primary_time <
  // b->primary_time.
  static bool ConfigPrimaryTimeLessThan(
      const QuicReferenceCountedPointer<Config>& a,
      const QuicReferenceCountedPointer<Config>& b);

  // SelectNewPrimaryConfig reevaluates the primary config based on the
  // "primary_time" deadlines contained in each.
  void SelectNewPrimaryConfig(QuicWallTime now) const
      QUIC_EXCLUSIVE_LOCKS_REQUIRED(configs_lock_);

  // EvaluateClientHello checks |client_hello_state->client_hello| for gross
  // errors and determines whether it is fresh (i.e. not a replay). The results
  // are written to |client_hello_state->info|.
  void EvaluateClientHello(
      const QuicSocketAddress& server_address,
      QuicTransportVersion version,
      const Configs& configs,
      QuicReferenceCountedPointer<ValidateClientHelloResultCallback::Result>
          client_hello_state,
      std::unique_ptr<ValidateClientHelloResultCallback> done_cb) const;

  // Convenience class which carries the arguments passed to
  // |ProcessClientHellp| along.
  class QUIC_EXPORT_PRIVATE ProcessClientHelloContext {
   public:
    ProcessClientHelloContext(
        QuicReferenceCountedPointer<ValidateClientHelloResultCallback::Result>
            validate_chlo_result,
        bool reject_only,
        QuicConnectionId connection_id,
        const QuicSocketAddress& server_address,
        const QuicSocketAddress& client_address,
        ParsedQuicVersion version,
        const ParsedQuicVersionVector& supported_versions,
        const QuicClock* clock,
        QuicRandom* rand,
        QuicCompressedCertsCache* compressed_certs_cache,
        QuicReferenceCountedPointer<QuicCryptoNegotiatedParameters> params,
        QuicReferenceCountedPointer<QuicSignedServerConfig> signed_config,
        QuicByteCount total_framing_overhead,
        QuicByteCount chlo_packet_size,
        std::unique_ptr<ProcessClientHelloResultCallback> done_cb)
        : validate_chlo_result_(validate_chlo_result),
          reject_only_(reject_only),
          connection_id_(connection_id),
          server_address_(server_address),
          client_address_(client_address),
          version_(version),
          supported_versions_(supported_versions),
          clock_(clock),
          rand_(rand),
          compressed_certs_cache_(compressed_certs_cache),
          params_(params),
          signed_config_(signed_config),
          total_framing_overhead_(total_framing_overhead),
          chlo_packet_size_(chlo_packet_size),
          done_cb_(std::move(done_cb)) {}

    ~ProcessClientHelloContext();

    // Invoke |done_cb_| with an error status
    void Fail(QuicErrorCode error, const std::string& error_details);

    // Invoke |done_cb_| with a success status
    void Succeed(std::unique_ptr<CryptoHandshakeMessage> message,
                 std::unique_ptr<DiversificationNonce> diversification_nonce,
                 std::unique_ptr<ProofSource::Details> proof_source_details);

    // Member accessors
    QuicReferenceCountedPointer<ValidateClientHelloResultCallback::Result>
    validate_chlo_result() const {
      return validate_chlo_result_;
    }
    bool reject_only() const { return reject_only_; }
    QuicConnectionId connection_id() const { return connection_id_; }
    QuicSocketAddress server_address() const { return server_address_; }
    QuicSocketAddress client_address() const { return client_address_; }
    ParsedQuicVersion version() const { return version_; }
    ParsedQuicVersionVector supported_versions() const {
      return supported_versions_;
    }
    const QuicClock* clock() const { return clock_; }
    QuicRandom* rand() const { return rand_; }  // NOLINT
    QuicCompressedCertsCache* compressed_certs_cache() const {
      return compressed_certs_cache_;
    }
    QuicReferenceCountedPointer<QuicCryptoNegotiatedParameters> params() const {
      return params_;
    }
    QuicReferenceCountedPointer<QuicSignedServerConfig> signed_config() const {
      return signed_config_;
    }
    QuicByteCount total_framing_overhead() const {
      return total_framing_overhead_;
    }
    QuicByteCount chlo_packet_size() const { return chlo_packet_size_; }

    // Derived value accessors
    const CryptoHandshakeMessage& client_hello() const {
      return validate_chlo_result()->client_hello;
    }
    const ClientHelloInfo& info() const { return validate_chlo_result()->info; }
    QuicTransportVersion transport_version() const {
      return version().transport_version;
    }

   private:
    const QuicReferenceCountedPointer<ValidateClientHelloResultCallback::Result>
        validate_chlo_result_;
    const bool reject_only_;
    const QuicConnectionId connection_id_;
    const QuicSocketAddress server_address_;
    const QuicSocketAddress client_address_;
    const ParsedQuicVersion version_;
    const ParsedQuicVersionVector supported_versions_;
    const QuicClock* const clock_;
    QuicRandom* const rand_;
    QuicCompressedCertsCache* const compressed_certs_cache_;
    const QuicReferenceCountedPointer<QuicCryptoNegotiatedParameters> params_;
    const QuicReferenceCountedPointer<QuicSignedServerConfig> signed_config_;
    const QuicByteCount total_framing_overhead_;
    const QuicByteCount chlo_packet_size_;
    std::unique_ptr<ProcessClientHelloResultCallback> done_cb_;
  };

  // Callback class for bridging between ProcessClientHello and
  // ProcessClientHelloAfterGetProof.
  class ProcessClientHelloCallback;
  friend class ProcessClientHelloCallback;

  // Portion of ProcessClientHello which executes after GetProof.
  void ProcessClientHelloAfterGetProof(
      bool found_error,
      std::unique_ptr<ProofSource::Details> proof_source_details,
      std::unique_ptr<ProcessClientHelloContext> context,
      const Configs& configs) const;

  // Callback class for bridging between ProcessClientHelloAfterGetProof and
  // ProcessClientHelloAfterCalculateSharedKeys.
  class ProcessClientHelloAfterGetProofCallback;
  friend class ProcessClientHelloAfterGetProofCallback;

  // Portion of ProcessClientHello which executes after CalculateSharedKeys.
  void ProcessClientHelloAfterCalculateSharedKeys(
      bool found_error,
      std::unique_ptr<ProofSource::Details> proof_source_details,
      QuicTag key_exchange_type,
      std::unique_ptr<CryptoHandshakeMessage> out,
      quiche::QuicheStringPiece public_value,
      std::unique_ptr<ProcessClientHelloContext> context,
      const Configs& configs) const;

  // Send a REJ which contains a different ServerConfig than the one the client
  // originally used.  This is necessary in cases where we discover in the
  // middle of the handshake that the private key for the ServerConfig the
  // client used is not accessible.
  void SendRejectWithFallbackConfig(
      std::unique_ptr<ProcessClientHelloContext> context,
      QuicReferenceCountedPointer<Config> fallback_config) const;

  // Callback class for bridging between SendRejectWithFallbackConfig and
  // SendRejectWithFallbackConfigAfterGetProof.
  class SendRejectWithFallbackConfigCallback;
  friend class SendRejectWithFallbackConfigCallback;

  // Portion of ProcessClientHello which executes after GetProof in the case
  // where we have received a CHLO but need to reject it due to the ServerConfig
  // private keys being inaccessible.
  void SendRejectWithFallbackConfigAfterGetProof(
      bool found_error,
      std::unique_ptr<ProofSource::Details> proof_source_details,
      std::unique_ptr<ProcessClientHelloContext> context,
      QuicReferenceCountedPointer<Config> fallback_config) const;

  // BuildRejectionAndRecordStats calls |BuildRejection| below and also informs
  // the RejectionObserver.
  void BuildRejectionAndRecordStats(const ProcessClientHelloContext& context,
                                    const Config& config,
                                    const std::vector<uint32_t>& reject_reasons,
                                    CryptoHandshakeMessage* out) const;

  // BuildRejection sets |out| to be a REJ message in reply to |client_hello|.
  void BuildRejection(const ProcessClientHelloContext& context,
                      const Config& config,
                      const std::vector<uint32_t>& reject_reasons,
                      CryptoHandshakeMessage* out) const;

  // CompressChain compresses the certificates in |chain->certs| and returns a
  // compressed representation. |common_sets| contains the common certificate
  // sets known locally and |client_common_set_hashes| contains the hashes of
  // the common sets known to the peer. |client_cached_cert_hashes| contains
  // 64-bit, FNV-1a hashes of certificates that the peer already possesses.
  static std::string CompressChain(
      QuicCompressedCertsCache* compressed_certs_cache,
      const QuicReferenceCountedPointer<ProofSource::Chain>& chain,
      const std::string& client_common_set_hashes,
      const std::string& client_cached_cert_hashes,
      const CommonCertSets* common_sets);

  // ParseConfigProtobuf parses the given config protobuf and returns a
  // QuicReferenceCountedPointer<Config> if successful. The caller adopts the
  // reference to the Config. On error, ParseConfigProtobuf returns nullptr.
  QuicReferenceCountedPointer<Config> ParseConfigProtobuf(
      const QuicServerConfigProtobuf& protobuf,
      bool is_fallback) const;

  // NewSourceAddressToken returns a fresh source address token for the given
  // IP address. |cached_network_params| is optional, and can be nullptr.
  std::string NewSourceAddressToken(
      const Config& config,
      const SourceAddressTokens& previous_tokens,
      const QuicIpAddress& ip,
      QuicRandom* rand,
      QuicWallTime now,
      const CachedNetworkParameters* cached_network_params) const;

  // ParseSourceAddressToken parses the source address tokens contained in
  // the encrypted |token|, and populates |tokens| with the parsed tokens.
  // Returns HANDSHAKE_OK if |token| could be parsed, or the reason for the
  // failure.
  HandshakeFailureReason ParseSourceAddressToken(
      const Config& config,
      quiche::QuicheStringPiece token,
      SourceAddressTokens* tokens) const;

  // ValidateSourceAddressTokens returns HANDSHAKE_OK if the source address
  // tokens in |tokens| contain a valid and timely token for the IP address
  // |ip| given that the current time is |now|. Otherwise it returns the
  // reason for failure. |cached_network_params| is populated if the valid
  // token contains a CachedNetworkParameters proto.
  HandshakeFailureReason ValidateSourceAddressTokens(
      const SourceAddressTokens& tokens,
      const QuicIpAddress& ip,
      QuicWallTime now,
      CachedNetworkParameters* cached_network_params) const;

  // ValidateSingleSourceAddressToken returns HANDSHAKE_OK if the source
  // address token in |token| is a timely token for the IP address |ip|
  // given that the current time is |now|. Otherwise it returns the reason
  // for failure.
  HandshakeFailureReason ValidateSingleSourceAddressToken(
      const SourceAddressToken& token,
      const QuicIpAddress& ip,
      QuicWallTime now) const;

  // Returns HANDSHAKE_OK if the source address token in |token| is a timely
  // token given that the current time is |now|. Otherwise it returns the
  // reason for failure.
  HandshakeFailureReason ValidateSourceAddressTokenTimestamp(
      const SourceAddressToken& token,
      QuicWallTime now) const;

  // NewServerNonce generates and encrypts a random nonce.
  std::string NewServerNonce(QuicRandom* rand, QuicWallTime now) const;

  // ValidateExpectedLeafCertificate checks the |client_hello| to see if it has
  // an XLCT tag, and if so, verifies that its value matches the hash of the
  // server's leaf certificate. |certs| is used to compare against the XLCT
  // value.  This method returns true if the XLCT tag is not present, or if the
  // XLCT tag is present and valid. It returns false otherwise.
  bool ValidateExpectedLeafCertificate(
      const CryptoHandshakeMessage& client_hello,
      const std::vector<std::string>& certs) const;

  // Callback to receive the results of ProofSource::GetProof.  Note: this
  // callback has no cancellation support, since the lifetime of the ProofSource
  // is controlled by this object via unique ownership.  If that ownership
  // stricture changes, this decision may need to be revisited.
  class BuildServerConfigUpdateMessageProofSourceCallback
      : public ProofSource::Callback {
   public:
    BuildServerConfigUpdateMessageProofSourceCallback(
        const BuildServerConfigUpdateMessageProofSourceCallback&) = delete;
    ~BuildServerConfigUpdateMessageProofSourceCallback() override;
    void operator=(const BuildServerConfigUpdateMessageProofSourceCallback&) =
        delete;
    BuildServerConfigUpdateMessageProofSourceCallback(
        const QuicCryptoServerConfig* config,
        QuicCompressedCertsCache* compressed_certs_cache,
        const CommonCertSets* common_cert_sets,
        const QuicCryptoNegotiatedParameters& params,
        CryptoHandshakeMessage message,
        std::unique_ptr<BuildServerConfigUpdateMessageResultCallback> cb);

    void Run(bool ok,
             const QuicReferenceCountedPointer<ProofSource::Chain>& chain,
             const QuicCryptoProof& proof,
             std::unique_ptr<ProofSource::Details> details) override;

   private:
    const QuicCryptoServerConfig* config_;
    QuicCompressedCertsCache* compressed_certs_cache_;
    const CommonCertSets* common_cert_sets_;
    const std::string client_common_set_hashes_;
    const std::string client_cached_cert_hashes_;
    const bool sct_supported_by_client_;
    const std::string sni_;
    CryptoHandshakeMessage message_;
    std::unique_ptr<BuildServerConfigUpdateMessageResultCallback> cb_;
  };

  // Invoked by BuildServerConfigUpdateMessageProofSourceCallback::Run once
  // the proof has been acquired.  Finishes building the server config update
  // message and invokes |cb|.
  void FinishBuildServerConfigUpdateMessage(
      QuicCompressedCertsCache* compressed_certs_cache,
      const CommonCertSets* common_cert_sets,
      const std::string& client_common_set_hashes,
      const std::string& client_cached_cert_hashes,
      bool sct_supported_by_client,
      const std::string& sni,
      bool ok,
      const QuicReferenceCountedPointer<ProofSource::Chain>& chain,
      const std::string& signature,
      const std::string& leaf_cert_sct,
      std::unique_ptr<ProofSource::Details> details,
      CryptoHandshakeMessage message,
      std::unique_ptr<BuildServerConfigUpdateMessageResultCallback> cb) const;

  // Returns true if the next config promotion should happen now.
  bool IsNextConfigReady(QuicWallTime now) const
      QUIC_SHARED_LOCKS_REQUIRED(configs_lock_);

  // replay_protection_ controls whether the server enforces that handshakes
  // aren't replays.
  bool replay_protection_;

  // The multiple of the CHLO message size that a REJ message must stay under
  // when the client doesn't present a valid source-address token. This is
  // used to protect QUIC from amplification attacks.
  size_t chlo_multiplier_;

  // configs_ satisfies the following invariants:
  //   1) configs_.empty() <-> primary_config_ == nullptr
  //   2) primary_config_ != nullptr -> primary_config_->is_primary
  //   3) ∀ c∈configs_, c->is_primary <-> c == primary_config_
  mutable QuicMutex configs_lock_;

  // configs_ contains all active server configs. It's expected that there are
  // about half-a-dozen configs active at any one time.
  ConfigMap configs_ QUIC_GUARDED_BY(configs_lock_);

  // primary_config_ points to a Config (which is also in |configs_|) which is
  // the primary config - i.e. the one that we'll give out to new clients.
  mutable QuicReferenceCountedPointer<Config> primary_config_
      QUIC_GUARDED_BY(configs_lock_);

  // fallback_config_ points to a Config (which is also in |configs_|) which is
  // the fallback config, which will be used if the other configs are unuseable
  // for some reason.
  //
  // TODO(b/112548056): This is currently always nullptr.
  QuicReferenceCountedPointer<Config> fallback_config_
      QUIC_GUARDED_BY(configs_lock_);

  // next_config_promotion_time_ contains the nearest, future time when an
  // active config will be promoted to primary.
  mutable QuicWallTime next_config_promotion_time_
      QUIC_GUARDED_BY(configs_lock_);

  // Callback to invoke when the primary config changes.
  std::unique_ptr<PrimaryConfigChangedCallback> primary_config_changed_cb_
      QUIC_GUARDED_BY(configs_lock_);

  // Used to protect the source-address tokens that are given to clients.
  CryptoSecretBoxer source_address_token_boxer_;

  // server_nonce_boxer_ is used to encrypt and validate suggested server
  // nonces.
  CryptoSecretBoxer server_nonce_boxer_;

  // server_nonce_orbit_ contains the random, per-server orbit values that this
  // server will use to generate server nonces (the moral equivalent of a SYN
  // cookies).
  uint8_t server_nonce_orbit_[8];

  // proof_source_ contains an object that can provide certificate chains and
  // signatures.
  std::unique_ptr<ProofSource> proof_source_;
  std::unique_ptr<ServerProofVerifier> proof_verifier_;
  ClientCertMode client_cert_mode_;

  // key_exchange_source_ contains an object that can provide key exchange
  // objects.
  std::unique_ptr<KeyExchangeSource> key_exchange_source_;

  // ssl_ctx_ contains the server configuration for doing TLS handshakes.
  bssl::UniquePtr<SSL_CTX> ssl_ctx_;

  // These fields store configuration values. See the comments for their
  // respective setter functions.
  uint32_t source_address_token_future_secs_;
  uint32_t source_address_token_lifetime_secs_;

  // Enable serving SCT or not.
  bool enable_serving_sct_;

  // Does not own this observer.
  RejectionObserver* rejection_observer_;

  // If non-empty, the server will operate in the pre-shared key mode by
  // incorporating |pre_shared_key_| into the key schedule.
  std::string pre_shared_key_;

  // Whether REJ message should be padded to max packet size.
  bool pad_rej_;

  // Whether SHLO message should be padded to max packet size.
  bool pad_shlo_;

  // If client is allowed to send a small client hello (by disabling padding),
  // server MUST not check for the client hello size.
  // DO NOT disable this unless you have some other way of validating client.
  // (e.g. in realtime scenarios, where quic is tunneled through ICE, ICE will
  // do its own peer validation using STUN pings with ufrag/upass).
  bool validate_chlo_size_;

  // When source address is validated by some other means (e.g. when using ICE),
  // source address token validation may be disabled.
  bool validate_source_address_token_;
};

struct QUIC_EXPORT_PRIVATE QuicSignedServerConfig
    : public QuicReferenceCounted {
  QuicSignedServerConfig();

  QuicCryptoProof proof;
  QuicReferenceCountedPointer<ProofSource::Chain> chain;
  // The server config that is used for this proof (and the rest of the
  // request).
  QuicReferenceCountedPointer<QuicCryptoServerConfig::Config> config;
  std::string primary_scid;

 protected:
  ~QuicSignedServerConfig() override;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_QUIC_CRYPTO_SERVER_CONFIG_H_
