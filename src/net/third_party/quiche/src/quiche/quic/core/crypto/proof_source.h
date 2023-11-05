// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_PROOF_SOURCE_H_
#define QUICHE_QUIC_CORE_CRYPTO_PROOF_SOURCE_H_

#include <memory>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "openssl/ssl.h"
#include "quiche/quic/core/crypto/certificate_view.h"
#include "quiche/quic/core/crypto/quic_crypto_proof.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/common/platform/api/quiche_reference_counted.h"

namespace quic {

namespace test {
class FakeProofSourceHandle;
}  // namespace test

// CryptoBuffers is a RAII class to own a std::vector<CRYPTO_BUFFER*> and the
// buffers the elements point to.
struct QUICHE_EXPORT CryptoBuffers {
  CryptoBuffers() = default;
  CryptoBuffers(const CryptoBuffers&) = delete;
  CryptoBuffers(CryptoBuffers&&) = default;
  ~CryptoBuffers();

  std::vector<CRYPTO_BUFFER*> value;
};

// ProofSource is an interface by which a QUIC server can obtain certificate
// chains and signatures that prove its identity.
class QUICHE_EXPORT ProofSource {
 public:
  // Chain is a reference-counted wrapper for a vector of stringified
  // certificates.
  struct QUICHE_EXPORT Chain : public quiche::QuicheReferenceCounted {
    explicit Chain(const std::vector<std::string>& certs);
    Chain(const Chain&) = delete;
    Chain& operator=(const Chain&) = delete;

    CryptoBuffers ToCryptoBuffers() const;

    const std::vector<std::string> certs;

   protected:
    ~Chain() override;
  };

  // Details is an abstract class which acts as a container for any
  // implementation-specific details that a ProofSource wants to return.
  class QUICHE_EXPORT Details {
   public:
    virtual ~Details() {}
  };

  // Callback base class for receiving the results of an async call to GetProof.
  class QUICHE_EXPORT Callback {
   public:
    Callback() {}
    virtual ~Callback() {}

    // Invoked upon completion of GetProof.
    //
    // |ok| indicates whether the operation completed successfully.  If false,
    // the values of the remaining three arguments are undefined.
    //
    // |chain| is a reference-counted pointer to an object representing the
    // certificate chain.
    //
    // |signature| contains the signature of the server config.
    //
    // |leaf_cert_sct| holds the signed timestamp (RFC6962) of the leaf cert.
    //
    // |details| holds a pointer to an object representing the statistics, if
    // any, gathered during the operation of GetProof.  If no stats are
    // available, this will be nullptr.
    virtual void Run(bool ok,
                     const quiche::QuicheReferenceCountedPointer<Chain>& chain,
                     const QuicCryptoProof& proof,
                     std::unique_ptr<Details> details) = 0;

   private:
    Callback(const Callback&) = delete;
    Callback& operator=(const Callback&) = delete;
  };

  // Base class for signalling the completion of a call to ComputeTlsSignature.
  class QUICHE_EXPORT SignatureCallback {
   public:
    SignatureCallback() {}
    virtual ~SignatureCallback() = default;

    // Invoked upon completion of ComputeTlsSignature.
    //
    // |ok| indicates whether the operation completed successfully.
    //
    // |signature| contains the signature of the data provided to
    // ComputeTlsSignature. Its value is undefined if |ok| is false.
    //
    // |details| holds a pointer to an object representing the statistics, if
    // any, gathered during the operation of ComputeTlsSignature.  If no stats
    // are available, this will be nullptr.
    virtual void Run(bool ok, std::string signature,
                     std::unique_ptr<Details> details) = 0;

   private:
    SignatureCallback(const SignatureCallback&) = delete;
    SignatureCallback& operator=(const SignatureCallback&) = delete;
  };

  virtual ~ProofSource() {}

  // OnNewSslCtx changes SSL parameters if required by ProofSource
  // implementation. It is called when new SSL_CTX is created for a listener.
  // Default implementation does nothing.
  //
  // This function may be called concurrently.
  virtual void OnNewSslCtx(SSL_CTX* ssl_ctx);

  // GetProof finds a certificate chain for |hostname| (in leaf-first order),
  // and calculates a signature of |server_config| using that chain.
  //
  // The signature uses SHA-256 as the hash function and PSS padding when the
  // key is RSA.
  //
  // The signature uses SHA-256 as the hash function when the key is ECDSA.
  // The signature may use an ECDSA key.
  //
  // The signature depends on |chlo_hash| which means that the signature can not
  // be cached.
  //
  // |hostname| may be empty to signify that a default certificate should be
  // used.
  //
  // This function may be called concurrently.
  //
  // Callers should expect that |callback| might be invoked synchronously.
  virtual void GetProof(const QuicSocketAddress& server_address,
                        const QuicSocketAddress& client_address,
                        const std::string& hostname,
                        const std::string& server_config,
                        QuicTransportVersion transport_version,
                        absl::string_view chlo_hash,
                        std::unique_ptr<Callback> callback) = 0;

  // Returns the certificate chain for |hostname| in leaf-first order.
  //
  // Sets *cert_matched_sni to true if the certificate matched the given
  // hostname, false if a default cert not matching the hostname was used.
  virtual quiche::QuicheReferenceCountedPointer<Chain> GetCertChain(
      const QuicSocketAddress& server_address,
      const QuicSocketAddress& client_address, const std::string& hostname,
      bool* cert_matched_sni) = 0;

  // Computes a signature using the private key of the certificate for
  // |hostname|. The value in |in| is signed using the algorithm specified by
  // |signature_algorithm|, which is an |SSL_SIGN_*| value (as defined in TLS
  // 1.3). Implementations can only assume that |in| is valid during the call to
  // ComputeTlsSignature - an implementation computing signatures asynchronously
  // must copy it if the value to be signed is used outside of this function.
  //
  // Callers should expect that |callback| might be invoked synchronously.
  virtual void ComputeTlsSignature(
      const QuicSocketAddress& server_address,
      const QuicSocketAddress& client_address, const std::string& hostname,
      uint16_t signature_algorithm, absl::string_view in,
      std::unique_ptr<SignatureCallback> callback) = 0;

  // Return the list of TLS signature algorithms that is acceptable by the
  // ComputeTlsSignature method. If the entire BoringSSL's default list of
  // supported signature algorithms are acceptable, return an empty list.
  //
  // If returns a non-empty list, ComputeTlsSignature will only be called with a
  // algorithm in the list.
  virtual QuicSignatureAlgorithmVector SupportedTlsSignatureAlgorithms()
      const = 0;

  class QUICHE_EXPORT DecryptCallback {
   public:
    DecryptCallback() = default;
    virtual ~DecryptCallback() = default;

    virtual void Run(std::vector<uint8_t> plaintext) = 0;

   private:
    DecryptCallback(const Callback&) = delete;
    DecryptCallback& operator=(const Callback&) = delete;
  };

  // TicketCrypter is an interface for managing encryption and decryption of TLS
  // session tickets. A TicketCrypter gets used as an
  // SSL_CTX_set_ticket_aead_method in BoringSSL, which has a synchronous
  // Encrypt/Seal operation and a potentially asynchronous Decrypt/Open
  // operation. This interface allows for ticket decryptions to be performed on
  // a remote service.
  class QUICHE_EXPORT TicketCrypter {
   public:
    TicketCrypter() = default;
    virtual ~TicketCrypter() = default;

    // MaxOverhead returns the maximum number of bytes of overhead that may get
    // added when encrypting the ticket.
    virtual size_t MaxOverhead() = 0;

    // Encrypt takes a serialized TLS session ticket in |in|, encrypts it, and
    // returns the encrypted ticket. The resulting value must not be larger than
    // MaxOverhead bytes larger than |in|. If encryption fails, this method
    // returns an empty vector.
    //
    // If |encryption_key| is nonempty, this method should use it for minting
    // TLS resumption tickets.  If it is empty, this method may use an
    // internally cached encryption key, if available.
    virtual std::vector<uint8_t> Encrypt(absl::string_view in,
                                         absl::string_view encryption_key) = 0;

    // Decrypt takes an encrypted ticket |in|, decrypts it, and calls
    // |callback->Run| with the decrypted ticket, which must not be larger than
    // |in|. If decryption fails, the callback is invoked with an empty
    // vector.
    virtual void Decrypt(absl::string_view in,
                         std::shared_ptr<DecryptCallback> callback) = 0;
  };

  // Returns the TicketCrypter used for encrypting and decrypting TLS
  // session tickets, or nullptr if that functionality is not supported. The
  // TicketCrypter returned (if not nullptr) must be valid for the lifetime of
  // the ProofSource, and the caller does not take ownership of said
  // TicketCrypter.
  virtual TicketCrypter* GetTicketCrypter() = 0;
};

// ProofSourceHandleCallback is an interface that contains the callbacks when
// the operations in ProofSourceHandle completes.
// TODO(wub): Consider deprecating ProofSource by moving all functionalities of
// ProofSource into ProofSourceHandle.
class QUICHE_EXPORT ProofSourceHandleCallback {
 public:
  virtual ~ProofSourceHandleCallback() = default;

  // Called when a ProofSourceHandle::SelectCertificate operation completes.
  // |ok| indicates whether the operation was successful.
  // |is_sync| indicates whether the operation completed synchronously, i.e.
  //      whether it is completed before ProofSourceHandle::SelectCertificate
  //      returned.
  // |chain| the certificate chain in leaf-first order.
  // |handshake_hints| (optional) handshake hints that can be used by
  //      SSL_set_handshake_hints.
  // |ticket_encryption_key| (optional) encryption key to be used for minting
  //      TLS resumption tickets.
  // |cert_matched_sni| is true if the certificate matched the SNI hostname,
  //      false if a non-matching default cert was used.
  // |delayed_ssl_config| contains SSL configs to be applied on the SSL object.
  //
  // When called asynchronously(is_sync=false), this method will be responsible
  // to continue the handshake from where it left off.
  virtual void OnSelectCertificateDone(
      bool ok, bool is_sync, const ProofSource::Chain* chain,
      absl::string_view handshake_hints,
      absl::string_view ticket_encryption_key, bool cert_matched_sni,
      QuicDelayedSSLConfig delayed_ssl_config) = 0;

  // Called when a ProofSourceHandle::ComputeSignature operation completes.
  virtual void OnComputeSignatureDone(
      bool ok, bool is_sync, std::string signature,
      std::unique_ptr<ProofSource::Details> details) = 0;

  // Return true iff ProofSourceHandle::ComputeSignature won't be called later.
  // The handle can use this function to release resources promptly.
  virtual bool WillNotCallComputeSignature() const = 0;
};

// ProofSourceHandle is an interface by which a TlsServerHandshaker can obtain
// certificate chains and signatures that prove its identity.
// The operations this interface supports are similar to those in ProofSource,
// the main difference is that ProofSourceHandle is per-handshaker, so
// an implementation can have states that are shared by multiple calls on the
// same handle.
//
// A handle object is owned by a TlsServerHandshaker. Since there might be an
// async operation pending when the handle destructs, an implementation must
// ensure when such operations finish, their corresponding callback method won't
// be invoked.
//
// A handle will have at most one async operation pending at a time.
class QUICHE_EXPORT ProofSourceHandle {
 public:
  virtual ~ProofSourceHandle() = default;

  // Close the handle. Cancel the pending operation, if any.
  // Once called, any completion method on |callback()| won't be invoked, and
  // future SelectCertificate and ComputeSignature calls should return failure.
  virtual void CloseHandle() = 0;

  // Starts a select certificate operation. If the operation is not cancelled
  // when it completes, callback()->OnSelectCertificateDone will be invoked.
  //
  // server_address and client_address should be normalized by the caller before
  // sending down to this function.
  //
  // If the operation is handled synchronously:
  // - QUIC_SUCCESS or QUIC_FAILURE will be returned.
  // - callback()->OnSelectCertificateDone should be invoked before the function
  //   returns.
  //
  // If the operation is handled asynchronously:
  // - QUIC_PENDING will be returned.
  // - When the operation is done, callback()->OnSelectCertificateDone should be
  //   invoked.
  virtual QuicAsyncStatus SelectCertificate(
      const QuicSocketAddress& server_address,
      const QuicSocketAddress& client_address,
      const QuicConnectionId& original_connection_id,
      absl::string_view ssl_capabilities, const std::string& hostname,
      absl::string_view client_hello, const std::string& alpn,
      absl::optional<std::string> alps,
      const std::vector<uint8_t>& quic_transport_params,
      const absl::optional<std::vector<uint8_t>>& early_data_context,
      const QuicSSLConfig& ssl_config) = 0;

  // Starts a compute signature operation. If the operation is not cancelled
  // when it completes, callback()->OnComputeSignatureDone will be invoked.
  //
  // See the comments of SelectCertificate for sync vs. async operations.
  virtual QuicAsyncStatus ComputeSignature(
      const QuicSocketAddress& server_address,
      const QuicSocketAddress& client_address, const std::string& hostname,
      uint16_t signature_algorithm, absl::string_view in,
      size_t max_signature_size) = 0;

 protected:
  // Returns the object that will be notified when an operation completes.
  virtual ProofSourceHandleCallback* callback() = 0;

 private:
  friend class test::FakeProofSourceHandle;
};

// Returns true if |chain| contains a parsable DER-encoded X.509 leaf cert and
// it matches with |key|.
QUICHE_EXPORT bool ValidateCertAndKey(
    const quiche::QuicheReferenceCountedPointer<ProofSource::Chain>& chain,
    const CertificatePrivateKey& key);

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_PROOF_SOURCE_H_
