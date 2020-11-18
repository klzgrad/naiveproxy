// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_PROOF_SOURCE_H_
#define QUICHE_QUIC_CORE_CRYPTO_PROOF_SOURCE_H_

#include <memory>
#include <string>
#include <vector>

#include "third_party/boringssl/src/include/openssl/ssl.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_crypto_proof.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_reference_counted.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_socket_address.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

// CryptoBuffers is a RAII class to own a std::vector<CRYPTO_BUFFER*> and the
// buffers the elements point to.
struct QUIC_EXPORT_PRIVATE CryptoBuffers {
  CryptoBuffers() = default;
  CryptoBuffers(const CryptoBuffers&) = delete;
  CryptoBuffers(CryptoBuffers&&) = default;
  ~CryptoBuffers();

  std::vector<CRYPTO_BUFFER*> value;
};

// ProofSource is an interface by which a QUIC server can obtain certificate
// chains and signatures that prove its identity.
class QUIC_EXPORT_PRIVATE ProofSource {
 public:
  // Chain is a reference-counted wrapper for a vector of stringified
  // certificates.
  struct QUIC_EXPORT_PRIVATE Chain : public QuicReferenceCounted {
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
  class QUIC_EXPORT_PRIVATE Details {
   public:
    virtual ~Details() {}
  };

  // Callback base class for receiving the results of an async call to GetProof.
  class QUIC_EXPORT_PRIVATE Callback {
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
                     const QuicReferenceCountedPointer<Chain>& chain,
                     const QuicCryptoProof& proof,
                     std::unique_ptr<Details> details) = 0;

   private:
    Callback(const Callback&) = delete;
    Callback& operator=(const Callback&) = delete;
  };

  // Base class for signalling the completion of a call to ComputeTlsSignature.
  class QUIC_EXPORT_PRIVATE SignatureCallback {
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
    virtual void Run(bool ok,
                     std::string signature,
                     std::unique_ptr<Details> details) = 0;

   private:
    SignatureCallback(const SignatureCallback&) = delete;
    SignatureCallback& operator=(const SignatureCallback&) = delete;
  };

  virtual ~ProofSource() {}

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
                        quiche::QuicheStringPiece chlo_hash,
                        std::unique_ptr<Callback> callback) = 0;

  // Returns the certificate chain for |hostname| in leaf-first order.
  virtual QuicReferenceCountedPointer<Chain> GetCertChain(
      const QuicSocketAddress& server_address,
      const QuicSocketAddress& client_address,
      const std::string& hostname) = 0;

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
      const QuicSocketAddress& client_address,
      const std::string& hostname,
      uint16_t signature_algorithm,
      quiche::QuicheStringPiece in,
      std::unique_ptr<SignatureCallback> callback) = 0;

  class QUIC_EXPORT_PRIVATE DecryptCallback {
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
  class QUIC_EXPORT_PRIVATE TicketCrypter {
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
    virtual std::vector<uint8_t> Encrypt(quiche::QuicheStringPiece in) = 0;

    // Decrypt takes an encrypted ticket |in|, decrypts it, and calls
    // |callback->Run| with the decrypted ticket, which must not be larger than
    // |in|. If decryption fails, the callback is invoked with an empty
    // vector.
    virtual void Decrypt(quiche::QuicheStringPiece in,
                         std::unique_ptr<DecryptCallback> callback) = 0;
  };

  // Returns the TicketCrypter used for encrypting and decrypting TLS
  // session tickets, or nullptr if that functionality is not supported. The
  // TicketCrypter returned (if not nullptr) must be valid for the lifetime of
  // the ProofSource, and the caller does not take ownership of said
  // TicketCrypter.
  virtual TicketCrypter* GetTicketCrypter() = 0;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_PROOF_SOURCE_H_
