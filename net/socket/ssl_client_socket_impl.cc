// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/socket/ssl_client_socket_impl.h"

#include <errno.h>
#include <string.h>

#include <algorithm>
#include <utility>

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/lazy_instance.h"
#include "base/macros.h"
#include "base/memory/singleton.h"
#include "base/metrics/field_trial.h"
#include "base/metrics/histogram_macros.h"
#include "base/metrics/sparse_histogram.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_piece.h"
#include "base/strings/stringprintf.h"
#include "base/synchronization/lock.h"
#include "base/threading/thread_local.h"
#include "base/trace_event/process_memory_dump.h"
#include "base/trace_event/trace_event.h"
#include "base/values.h"
#include "crypto/ec_private_key.h"
#include "crypto/openssl_util.h"
#include "net/base/ip_address.h"
#include "net/base/net_errors.h"
#include "net/base/trace_constants.h"
#include "net/cert/cert_verifier.h"
#include "net/cert/ct_policy_enforcer.h"
#include "net/cert/ct_policy_status.h"
#include "net/cert/ct_verifier.h"
#include "net/cert/x509_certificate_net_log_param.h"
#include "net/cert/x509_util.h"
#include "net/http/transport_security_state.h"
#include "net/log/net_log.h"
#include "net/log/net_log_event_type.h"
#include "net/log/net_log_parameters_callback.h"
#include "net/ssl/ssl_cert_request_info.h"
#include "net/ssl/ssl_cipher_suite_names.h"
#include "net/ssl/ssl_client_session_cache.h"
#include "net/ssl/ssl_connection_status_flags.h"
#include "net/ssl/ssl_info.h"
#include "net/ssl/ssl_private_key.h"
#include "net/ssl/token_binding.h"
#include "third_party/boringssl/src/include/openssl/bio.h"
#include "third_party/boringssl/src/include/openssl/bytestring.h"
#include "third_party/boringssl/src/include/openssl/err.h"
#include "third_party/boringssl/src/include/openssl/evp.h"
#include "third_party/boringssl/src/include/openssl/mem.h"
#include "third_party/boringssl/src/include/openssl/ssl.h"

#if !defined(OS_NACL)
#include "net/ssl/ssl_key_logger.h"
#endif

#if defined(USE_NSS_CERTS)
#include "net/cert_net/nss_ocsp.h"
#endif

namespace net {

namespace {

// This constant can be any non-negative/non-zero value (eg: it does not
// overlap with any value of the net::Error range, including net::OK).
const int kNoPendingResult = 1;

// Default size of the internal BoringSSL buffers.
const int kDefaultOpenSSLBufferSize = 17 * 1024;

// TLS extension number use for Token Binding.
const unsigned int kTbExtNum = 24;

// Token Binding ProtocolVersions supported.
const uint8_t kTbProtocolVersionMajor = 0;
const uint8_t kTbProtocolVersionMinor = 13;
const uint8_t kTbMinProtocolVersionMajor = 0;
const uint8_t kTbMinProtocolVersionMinor = 10;

bool EVP_MDToPrivateKeyHash(const EVP_MD* md, SSLPrivateKey::Hash* hash) {
  switch (EVP_MD_type(md)) {
    case NID_md5_sha1:
      *hash = SSLPrivateKey::Hash::MD5_SHA1;
      return true;
    case NID_sha1:
      *hash = SSLPrivateKey::Hash::SHA1;
      return true;
    case NID_sha256:
      *hash = SSLPrivateKey::Hash::SHA256;
      return true;
    case NID_sha384:
      *hash = SSLPrivateKey::Hash::SHA384;
      return true;
    case NID_sha512:
      *hash = SSLPrivateKey::Hash::SHA512;
      return true;
    default:
      return false;
  }
}

std::unique_ptr<base::Value> NetLogPrivateKeyOperationCallback(
    SSLPrivateKey::Hash hash,
    NetLogCaptureMode mode) {
  std::string hash_str;
  switch (hash) {
    case SSLPrivateKey::Hash::MD5_SHA1:
      hash_str = "MD5_SHA1";
      break;
    case SSLPrivateKey::Hash::SHA1:
      hash_str = "SHA1";
      break;
    case SSLPrivateKey::Hash::SHA256:
      hash_str = "SHA256";
      break;
    case SSLPrivateKey::Hash::SHA384:
      hash_str = "SHA384";
      break;
    case SSLPrivateKey::Hash::SHA512:
      hash_str = "SHA512";
      break;
  }

  std::unique_ptr<base::DictionaryValue> value(new base::DictionaryValue);
  value->SetString("hash", hash_str);
  return std::move(value);
}

std::unique_ptr<base::Value> NetLogChannelIDLookupCallback(
    ChannelIDService* channel_id_service,
    NetLogCaptureMode capture_mode) {
  ChannelIDStore* store = channel_id_service->GetChannelIDStore();
  std::unique_ptr<base::DictionaryValue> dict(new base::DictionaryValue());
  dict->SetBoolean("ephemeral", store->IsEphemeral());
  dict->SetString("service", base::HexEncode(&channel_id_service,
                                             sizeof(channel_id_service)));
  dict->SetString("store", base::HexEncode(&store, sizeof(store)));
  return std::move(dict);
}

std::unique_ptr<base::Value> NetLogChannelIDLookupCompleteCallback(
    crypto::ECPrivateKey* key,
    int result,
    NetLogCaptureMode capture_mode) {
  std::unique_ptr<base::DictionaryValue> dict(new base::DictionaryValue());
  dict->SetInteger("net_error", result);
  std::string raw_key;
  if (result == OK && key && key->ExportRawPublicKey(&raw_key)) {
    std::string key_to_log = base::HexEncode(raw_key.data(), raw_key.length());
    dict->SetString("key", key_to_log);
  }
  return std::move(dict);
}

std::unique_ptr<base::Value> NetLogSSLInfoCallback(
    SSLClientSocketImpl* socket,
    NetLogCaptureMode capture_mode) {
  SSLInfo ssl_info;
  if (!socket->GetSSLInfo(&ssl_info))
    return nullptr;

  std::unique_ptr<base::DictionaryValue> dict(new base::DictionaryValue());
  const char* version_str;
  SSLVersionToString(&version_str,
                     SSLConnectionStatusToVersion(ssl_info.connection_status));
  dict->SetString("version", version_str);
  dict->SetBoolean("is_resumed",
                   ssl_info.handshake_type == SSLInfo::HANDSHAKE_RESUME);
  dict->SetInteger("cipher_suite", SSLConnectionStatusToCipherSuite(
                                       ssl_info.connection_status));

  dict->SetString("next_proto",
                  NextProtoToString(socket->GetNegotiatedProtocol()));

  return std::move(dict);
}

int GetBufferSize(const char* field_trial) {
  // Get buffer sizes from field trials, if possible. If values not present,
  // use default.  Also make sure values are in reasonable range.
  int buffer_size = kDefaultOpenSSLBufferSize;
#if !defined(OS_NACL)
  int override_buffer_size;
  if (base::StringToInt(base::FieldTrialList::FindFullName(field_trial),
                        &override_buffer_size)) {
    buffer_size = override_buffer_size;
    buffer_size = std::max(buffer_size, 1000);
    buffer_size = std::min(buffer_size, 2 * kDefaultOpenSSLBufferSize);
  }
#endif  // !defined(OS_NACL)
  return buffer_size;
}

std::unique_ptr<base::Value> NetLogSSLAlertCallback(
    const void* bytes,
    size_t len,
    NetLogCaptureMode capture_mode) {
  std::unique_ptr<base::DictionaryValue> dict(new base::DictionaryValue());
  dict->SetString("hex_encoded_bytes", base::HexEncode(bytes, len));
  return std::move(dict);
}

std::unique_ptr<base::Value> NetLogSSLMessageCallback(
    bool is_write,
    const void* bytes,
    size_t len,
    NetLogCaptureMode capture_mode) {
  std::unique_ptr<base::DictionaryValue> dict(new base::DictionaryValue());
  if (len == 0) {
    NOTREACHED();
    return std::move(dict);
  }

  // The handshake message type is the first byte. Include it so elided messages
  // still report their type.
  uint8_t type = reinterpret_cast<const uint8_t*>(bytes)[0];
  dict->SetInteger("type", type);

  // Elide client certificate messages unless logging socket bytes. The client
  // certificate does not contain information needed to impersonate the user
  // (that's the private key which isn't sent over the wire), but it may contain
  // information on the user's identity.
  if (!is_write || type != SSL3_MT_CERTIFICATE ||
      capture_mode.include_socket_bytes()) {
    dict->SetString("hex_encoded_bytes", base::HexEncode(bytes, len));
  }

  return std::move(dict);
}

}  // namespace

class SSLClientSocketImpl::SSLContext {
 public:
  static SSLContext* GetInstance() {
    return base::Singleton<SSLContext,
                           base::LeakySingletonTraits<SSLContext>>::get();
  }
  SSL_CTX* ssl_ctx() { return ssl_ctx_.get(); }
  SSLClientSessionCache* session_cache() { return &session_cache_; }

  SSLClientSocketImpl* GetClientSocketFromSSL(const SSL* ssl) {
    DCHECK(ssl);
    SSLClientSocketImpl* socket = static_cast<SSLClientSocketImpl*>(
        SSL_get_ex_data(ssl, ssl_socket_data_index_));
    DCHECK(socket);
    return socket;
  }

  bool SetClientSocketForSSL(SSL* ssl, SSLClientSocketImpl* socket) {
    return SSL_set_ex_data(ssl, ssl_socket_data_index_, socket) != 0;
  }

#if !defined(OS_NACL)
  void SetSSLKeyLogFile(const base::FilePath& path) {
    DCHECK(!ssl_key_logger_);
    ssl_key_logger_.reset(new SSLKeyLogger(path));
    SSL_CTX_set_keylog_callback(ssl_ctx_.get(), KeyLogCallback);
  }
#endif

  static const SSL_PRIVATE_KEY_METHOD kPrivateKeyMethod;

 private:
  friend struct base::DefaultSingletonTraits<SSLContext>;

  SSLContext() : session_cache_(SSLClientSessionCache::Config()) {
    crypto::EnsureOpenSSLInit();
    ssl_socket_data_index_ = SSL_get_ex_new_index(0, 0, 0, 0, 0);
    DCHECK_NE(ssl_socket_data_index_, -1);
    ssl_ctx_.reset(SSL_CTX_new(TLS_with_buffers_method()));
    SSL_CTX_set_cert_cb(ssl_ctx_.get(), ClientCertRequestCallback, NULL);

    // The server certificate is verified after the handshake in DoVerifyCert.
    SSL_CTX_set_custom_verify(ssl_ctx_.get(), SSL_VERIFY_PEER,
                              CertVerifyCallback);

    // Disable the internal session cache. Session caching is handled
    // externally (i.e. by SSLClientSessionCache).
    SSL_CTX_set_session_cache_mode(
        ssl_ctx_.get(), SSL_SESS_CACHE_CLIENT | SSL_SESS_CACHE_NO_INTERNAL);
    SSL_CTX_sess_set_new_cb(ssl_ctx_.get(), NewSessionCallback);
    SSL_CTX_set_timeout(ssl_ctx_.get(), 1 * 60 * 60 /* one hour */);

    SSL_CTX_set_grease_enabled(ssl_ctx_.get(), 1);

    // Deduplicate all certificates minted from the SSL_CTX in memory.
    SSL_CTX_set0_buffer_pool(ssl_ctx_.get(), x509_util::GetBufferPool());

    SSL_CTX_set_msg_callback(ssl_ctx_.get(), MessageCallback);

    if (!SSL_CTX_add_client_custom_ext(ssl_ctx_.get(), kTbExtNum,
                                       &TokenBindingAddCallback,
                                       &TokenBindingFreeCallback, nullptr,
                                       &TokenBindingParseCallback, nullptr)) {
      NOTREACHED();
    }
  }

  static int TokenBindingAddCallback(SSL* ssl,
                                     unsigned int extension_value,
                                     const uint8_t** out,
                                     size_t* out_len,
                                     int* out_alert_value,
                                     void* add_arg) {
    DCHECK_EQ(extension_value, kTbExtNum);
    SSLClientSocketImpl* socket =
        SSLClientSocketImpl::SSLContext::GetInstance()->GetClientSocketFromSSL(
            ssl);
    return socket->TokenBindingAdd(out, out_len, out_alert_value);
  }

  static void TokenBindingFreeCallback(SSL* ssl,
                                       unsigned extension_value,
                                       const uint8_t* out,
                                       void* add_arg) {
    DCHECK_EQ(extension_value, kTbExtNum);
    OPENSSL_free(const_cast<unsigned char*>(out));
  }

  static int TokenBindingParseCallback(SSL* ssl,
                                       unsigned int extension_value,
                                       const uint8_t* contents,
                                       size_t contents_len,
                                       int* out_alert_value,
                                       void* parse_arg) {
    DCHECK_EQ(extension_value, kTbExtNum);
    SSLClientSocketImpl* socket =
        SSLClientSocketImpl::SSLContext::GetInstance()->GetClientSocketFromSSL(
            ssl);
    return socket->TokenBindingParse(contents, contents_len, out_alert_value);
  }

  static int ClientCertRequestCallback(SSL* ssl, void* arg) {
    SSLClientSocketImpl* socket = GetInstance()->GetClientSocketFromSSL(ssl);
    DCHECK(socket);
    return socket->ClientCertRequestCallback(ssl);
  }

  static ssl_verify_result_t CertVerifyCallback(SSL* ssl, uint8_t* out_alert) {
    // The certificate is verified after the handshake in DoVerifyCert.
    return ssl_verify_ok;
  }

  static int NewSessionCallback(SSL* ssl, SSL_SESSION* session) {
    SSLClientSocketImpl* socket = GetInstance()->GetClientSocketFromSSL(ssl);
    return socket->NewSessionCallback(session);
  }

  static ssl_private_key_result_t PrivateKeySignDigestCallback(
      SSL* ssl,
      uint8_t* out,
      size_t* out_len,
      size_t max_out,
      const EVP_MD* md,
      const uint8_t* in,
      size_t in_len) {
    SSLClientSocketImpl* socket = GetInstance()->GetClientSocketFromSSL(ssl);
    return socket->PrivateKeySignDigestCallback(out, out_len, max_out, md, in,
                                                in_len);
  }

  static ssl_private_key_result_t PrivateKeyCompleteCallback(SSL* ssl,
                                                             uint8_t* out,
                                                             size_t* out_len,
                                                             size_t max_out) {
    SSLClientSocketImpl* socket = GetInstance()->GetClientSocketFromSSL(ssl);
    return socket->PrivateKeyCompleteCallback(out, out_len, max_out);
  }

#if !defined(OS_NACL)
  static void KeyLogCallback(const SSL* ssl, const char* line) {
    GetInstance()->ssl_key_logger_->WriteLine(line);
  }
#endif

  static void MessageCallback(int is_write,
                              int version,
                              int content_type,
                              const void* buf,
                              size_t len,
                              SSL* ssl,
                              void* arg) {
    SSLClientSocketImpl* socket = GetInstance()->GetClientSocketFromSSL(ssl);
    return socket->MessageCallback(is_write, content_type, buf, len);
  }

  // This is the index used with SSL_get_ex_data to retrieve the owner
  // SSLClientSocketImpl object from an SSL instance.
  int ssl_socket_data_index_;

  bssl::UniquePtr<SSL_CTX> ssl_ctx_;

#if !defined(OS_NACL)
  std::unique_ptr<SSLKeyLogger> ssl_key_logger_;
#endif

  // TODO(davidben): Use a separate cache per URLRequestContext.
  // https://crbug.com/458365
  //
  // TODO(davidben): Sessions should be invalidated on fatal
  // alerts. https://crbug.com/466352
  SSLClientSessionCache session_cache_;
};

// TODO(davidben): Switch from sign_digest to sign.
const SSL_PRIVATE_KEY_METHOD
    SSLClientSocketImpl::SSLContext::kPrivateKeyMethod = {
        nullptr /* type (unused) */,
        nullptr /* max_signature_len (unused) */,
        nullptr /* sign */,
        &SSLClientSocketImpl::SSLContext::PrivateKeySignDigestCallback,
        nullptr /* decrypt */,
        &SSLClientSocketImpl::SSLContext::PrivateKeyCompleteCallback,
};

// static
void SSLClientSocket::ClearSessionCache() {
  SSLClientSocketImpl::SSLContext* context =
      SSLClientSocketImpl::SSLContext::GetInstance();
  context->session_cache()->Flush();
}

SSLClientSocketImpl::SSLClientSocketImpl(
    std::unique_ptr<ClientSocketHandle> transport_socket,
    const HostPortPair& host_and_port,
    const SSLConfig& ssl_config,
    const SSLClientSocketContext& context)
    : pending_read_error_(kNoPendingResult),
      pending_read_ssl_error_(SSL_ERROR_NONE),
      completed_connect_(false),
      was_ever_used_(false),
      cert_verifier_(context.cert_verifier),
      cert_transparency_verifier_(context.cert_transparency_verifier),
      channel_id_service_(context.channel_id_service),
      tb_was_negotiated_(false),
      tb_negotiated_param_(TB_PARAM_ECDSAP256),
      tb_signature_map_(10),
      transport_(std::move(transport_socket)),
      host_and_port_(host_and_port),
      ssl_config_(ssl_config),
      ssl_session_cache_shard_(context.ssl_session_cache_shard),
      next_handshake_state_(STATE_NONE),
      disconnected_(false),
      negotiated_protocol_(kProtoUnknown),
      channel_id_sent_(false),
      certificate_verified_(false),
      certificate_requested_(false),
      signature_result_(kNoPendingResult),
      transport_security_state_(context.transport_security_state),
      policy_enforcer_(context.ct_policy_enforcer),
      pkp_bypassed_(false),
      connect_error_details_(SSLErrorDetails::kOther),
      net_log_(transport_->socket()->NetLog()),
      weak_factory_(this) {
  CHECK(cert_verifier_);
  CHECK(transport_security_state_);
  CHECK(cert_transparency_verifier_);
  CHECK(policy_enforcer_);
}

SSLClientSocketImpl::~SSLClientSocketImpl() {
  Disconnect();
}

#if !defined(OS_NACL)
void SSLClientSocketImpl::SetSSLKeyLogFile(
    const base::FilePath& ssl_keylog_file) {
  SSLContext::GetInstance()->SetSSLKeyLogFile(ssl_keylog_file);
}
#endif

void SSLClientSocketImpl::GetSSLCertRequestInfo(
    SSLCertRequestInfo* cert_request_info) {
  if (!ssl_) {
    NOTREACHED();
    return;
  }

  cert_request_info->host_and_port = host_and_port_;

  cert_request_info->cert_authorities.clear();
  const STACK_OF(CRYPTO_BUFFER)* authorities =
      SSL_get0_server_requested_CAs(ssl_.get());
  for (size_t i = 0; i < sk_CRYPTO_BUFFER_num(authorities); i++) {
    const CRYPTO_BUFFER* ca_name = sk_CRYPTO_BUFFER_value(authorities, i);
    cert_request_info->cert_authorities.push_back(
        std::string(reinterpret_cast<const char*>(CRYPTO_BUFFER_data(ca_name)),
                    CRYPTO_BUFFER_len(ca_name)));
  }

  cert_request_info->cert_key_types.clear();
  const uint8_t* client_cert_types;
  size_t num_client_cert_types =
      SSL_get0_certificate_types(ssl_.get(), &client_cert_types);
  for (size_t i = 0; i < num_client_cert_types; i++) {
    cert_request_info->cert_key_types.push_back(
        static_cast<SSLClientCertType>(client_cert_types[i]));
  }
}

ChannelIDService* SSLClientSocketImpl::GetChannelIDService() const {
  return channel_id_service_;
}

Error SSLClientSocketImpl::GetTokenBindingSignature(crypto::ECPrivateKey* key,
                                                    TokenBindingType tb_type,
                                                    std::vector<uint8_t>* out) {
  // The same key will be used across multiple requests to sign the same value,
  // so the signature is cached.
  std::string raw_public_key;
  if (!key->ExportRawPublicKey(&raw_public_key))
    return ERR_FAILED;
  auto it = tb_signature_map_.Get(std::make_pair(tb_type, raw_public_key));
  if (it != tb_signature_map_.end()) {
    *out = it->second;
    return OK;
  }

  uint8_t tb_ekm_buf[32];
  static const char kTokenBindingExporterLabel[] = "EXPORTER-Token-Binding";
  if (!SSL_export_keying_material(ssl_.get(), tb_ekm_buf, sizeof(tb_ekm_buf),
                                  kTokenBindingExporterLabel,
                                  strlen(kTokenBindingExporterLabel), nullptr,
                                  0, false /* no context */)) {
    return ERR_FAILED;
  }

  if (!CreateTokenBindingSignature(
          base::StringPiece(reinterpret_cast<char*>(tb_ekm_buf),
                            sizeof(tb_ekm_buf)),
          tb_type, key, out))
    return ERR_FAILED;

  tb_signature_map_.Put(std::make_pair(tb_type, raw_public_key), *out);
  return OK;
}

crypto::ECPrivateKey* SSLClientSocketImpl::GetChannelIDKey() const {
  return channel_id_key_.get();
}

SSLErrorDetails SSLClientSocketImpl::GetConnectErrorDetails() const {
  return connect_error_details_;
}

int SSLClientSocketImpl::ExportKeyingMaterial(const base::StringPiece& label,
                                              bool has_context,
                                              const base::StringPiece& context,
                                              unsigned char* out,
                                              unsigned int outlen) {
  if (!IsConnected())
    return ERR_SOCKET_NOT_CONNECTED;

  crypto::OpenSSLErrStackTracer err_tracer(FROM_HERE);

  if (!SSL_export_keying_material(
          ssl_.get(), out, outlen, label.data(), label.size(),
          reinterpret_cast<const unsigned char*>(context.data()),
          context.length(), has_context ? 1 : 0)) {
    LOG(ERROR) << "Failed to export keying material.";
    return ERR_FAILED;
  }

  return OK;
}

int SSLClientSocketImpl::Connect(const CompletionCallback& callback) {
  // Although StreamSocket does allow calling Connect() after Disconnect(),
  // this has never worked for layered sockets. CHECK to detect any consumers
  // reconnecting an SSL socket.
  //
  // TODO(davidben,mmenke): Remove this API feature. See
  // https://crbug.com/499289.
  CHECK(!disconnected_);

  net_log_.BeginEvent(NetLogEventType::SSL_CONNECT);

  // Set up new ssl object.
  int rv = Init();
  if (rv != OK) {
    LogConnectEndEvent(rv);
    return rv;
  }

  // Set SSL to client mode. Handshake happens in the loop below.
  SSL_set_connect_state(ssl_.get());

  next_handshake_state_ = STATE_HANDSHAKE;
  rv = DoHandshakeLoop(OK);
  if (rv == ERR_IO_PENDING) {
    user_connect_callback_ = callback;
  } else {
    LogConnectEndEvent(rv);
  }

  return rv > OK ? OK : rv;
}

void SSLClientSocketImpl::Disconnect() {
  disconnected_ = true;

  // Shut down anything that may call us back.
  cert_verifier_request_.reset();
  channel_id_request_.Cancel();
  weak_factory_.InvalidateWeakPtrs();
  transport_adapter_.reset();

  // Release user callbacks.
  user_connect_callback_.Reset();
  user_read_callback_.Reset();
  user_write_callback_.Reset();
  user_read_buf_ = NULL;
  user_read_buf_len_ = 0;
  user_write_buf_ = NULL;
  user_write_buf_len_ = 0;

  transport_->socket()->Disconnect();
}

bool SSLClientSocketImpl::IsConnected() const {
  // If the handshake has not yet completed or the socket has been explicitly
  // disconnected.
  if (!completed_connect_ || disconnected_)
    return false;
  // If an asynchronous operation is still pending.
  if (user_read_buf_.get() || user_write_buf_.get())
    return true;

  return transport_->socket()->IsConnected();
}

bool SSLClientSocketImpl::IsConnectedAndIdle() const {
  // If the handshake has not yet completed or the socket has been explicitly
  // disconnected.
  if (!completed_connect_ || disconnected_)
    return false;
  // If an asynchronous operation is still pending.
  if (user_read_buf_.get() || user_write_buf_.get())
    return false;

  // If there is data read from the network that has not yet been consumed, do
  // not treat the connection as idle.
  //
  // Note that this does not check whether there is ciphertext that has not yet
  // been flushed to the network. |Write| returns early, so this can cause race
  // conditions which cause a socket to not be treated reusable when it should
  // be. See https://crbug.com/466147.
  if (transport_adapter_->HasPendingReadData())
    return false;

  return transport_->socket()->IsConnectedAndIdle();
}

int SSLClientSocketImpl::GetPeerAddress(IPEndPoint* addressList) const {
  return transport_->socket()->GetPeerAddress(addressList);
}

int SSLClientSocketImpl::GetLocalAddress(IPEndPoint* addressList) const {
  return transport_->socket()->GetLocalAddress(addressList);
}

const NetLogWithSource& SSLClientSocketImpl::NetLog() const {
  return net_log_;
}

void SSLClientSocketImpl::SetSubresourceSpeculation() {
  if (transport_.get() && transport_->socket()) {
    transport_->socket()->SetSubresourceSpeculation();
  } else {
    NOTREACHED();
  }
}

void SSLClientSocketImpl::SetOmniboxSpeculation() {
  if (transport_.get() && transport_->socket()) {
    transport_->socket()->SetOmniboxSpeculation();
  } else {
    NOTREACHED();
  }
}

bool SSLClientSocketImpl::WasEverUsed() const {
  return was_ever_used_;
}

bool SSLClientSocketImpl::WasAlpnNegotiated() const {
  return negotiated_protocol_ != kProtoUnknown;
}

NextProto SSLClientSocketImpl::GetNegotiatedProtocol() const {
  return negotiated_protocol_;
}

bool SSLClientSocketImpl::GetSSLInfo(SSLInfo* ssl_info) {
  ssl_info->Reset();
  if (!server_cert_)
    return false;

  ssl_info->cert = server_cert_verify_result_.verified_cert;
  ssl_info->unverified_cert = server_cert_;
  ssl_info->cert_status = server_cert_verify_result_.cert_status;
  ssl_info->is_issued_by_known_root =
      server_cert_verify_result_.is_issued_by_known_root;
  ssl_info->pkp_bypassed = pkp_bypassed_;
  ssl_info->public_key_hashes = server_cert_verify_result_.public_key_hashes;
  ssl_info->client_cert_sent =
      ssl_config_.send_client_cert && ssl_config_.client_cert.get();
  ssl_info->channel_id_sent = channel_id_sent_;
  ssl_info->token_binding_negotiated = tb_was_negotiated_;
  ssl_info->token_binding_key_param = tb_negotiated_param_;
  ssl_info->pinning_failure_log = pinning_failure_log_;
  ssl_info->ocsp_result = server_cert_verify_result_.ocsp_result;

  AddCTInfoToSSLInfo(ssl_info);

  const SSL_CIPHER* cipher = SSL_get_current_cipher(ssl_.get());
  CHECK(cipher);
  ssl_info->security_bits = SSL_CIPHER_get_bits(cipher, NULL);
  // Historically, the "group" was known as "curve".
  ssl_info->key_exchange_group = SSL_get_curve_id(ssl_.get());

  SSLConnectionStatusSetCipherSuite(
      static_cast<uint16_t>(SSL_CIPHER_get_id(cipher)),
      &ssl_info->connection_status);
  SSLConnectionStatusSetVersion(GetNetSSLVersion(ssl_.get()),
                                &ssl_info->connection_status);

  ssl_info->handshake_type = SSL_session_reused(ssl_.get())
                                 ? SSLInfo::HANDSHAKE_RESUME
                                 : SSLInfo::HANDSHAKE_FULL;

  return true;
}

void SSLClientSocketImpl::GetConnectionAttempts(ConnectionAttempts* out) const {
  out->clear();
}

int64_t SSLClientSocketImpl::GetTotalReceivedBytes() const {
  return transport_->socket()->GetTotalReceivedBytes();
}

void SSLClientSocketImpl::DumpMemoryStats(SocketMemoryStats* stats) const {
  if (transport_adapter_)
    stats->buffer_size = transport_adapter_->GetAllocationSize();
  const STACK_OF(CRYPTO_BUFFER)* server_cert_chain =
      SSL_get0_peer_certificates(ssl_.get());
  if (server_cert_chain) {
    for (size_t i = 0; i < sk_CRYPTO_BUFFER_num(server_cert_chain); ++i) {
      const CRYPTO_BUFFER* cert = sk_CRYPTO_BUFFER_value(server_cert_chain, i);
      stats->cert_size += CRYPTO_BUFFER_len(cert);
    }
    stats->cert_count = sk_CRYPTO_BUFFER_num(server_cert_chain);
  }
  stats->total_size = stats->buffer_size + stats->cert_size;
}

// static
void SSLClientSocketImpl::DumpSSLClientSessionMemoryStats(
    base::trace_event::ProcessMemoryDump* pmd) {
  SSLContext::GetInstance()->session_cache()->DumpMemoryStats(pmd);
}

int SSLClientSocketImpl::Read(IOBuffer* buf,
                              int buf_len,
                              const CompletionCallback& callback) {
  int rv = ReadIfReady(buf, buf_len, callback);
  if (rv == ERR_IO_PENDING) {
    user_read_buf_ = buf;
    user_read_buf_len_ = buf_len;
  }
  return rv;
}

int SSLClientSocketImpl::ReadIfReady(IOBuffer* buf,
                                     int buf_len,
                                     const CompletionCallback& callback) {
  int rv = DoPayloadRead(buf, buf_len);

  if (rv == ERR_IO_PENDING) {
    user_read_callback_ = callback;
  } else {
    if (rv > 0)
      was_ever_used_ = true;
  }
  return rv;
}

int SSLClientSocketImpl::Write(IOBuffer* buf,
                               int buf_len,
                               const CompletionCallback& callback) {
  user_write_buf_ = buf;
  user_write_buf_len_ = buf_len;

  int rv = DoPayloadWrite();

  if (rv == ERR_IO_PENDING) {
    user_write_callback_ = callback;
  } else {
    if (rv > 0)
      was_ever_used_ = true;
    user_write_buf_ = NULL;
    user_write_buf_len_ = 0;
  }

  return rv;
}

int SSLClientSocketImpl::SetReceiveBufferSize(int32_t size) {
  return transport_->socket()->SetReceiveBufferSize(size);
}

int SSLClientSocketImpl::SetSendBufferSize(int32_t size) {
  return transport_->socket()->SetSendBufferSize(size);
}

void SSLClientSocketImpl::OnReadReady() {
  // During a renegotiation, either Read or Write calls may be blocked on a
  // transport read.
  RetryAllOperations();
}

void SSLClientSocketImpl::OnWriteReady() {
  // During a renegotiation, either Read or Write calls may be blocked on a
  // transport read.
  RetryAllOperations();
}

int SSLClientSocketImpl::Init() {
  DCHECK(!ssl_);

#if defined(USE_NSS_CERTS)
  if (ssl_config_.cert_io_enabled) {
    // TODO(davidben): Move this out of SSLClientSocket. See
    // https://crbug.com/539520.
    EnsureNSSHttpIOInit();
  }
#endif

  SSLContext* context = SSLContext::GetInstance();
  crypto::OpenSSLErrStackTracer err_tracer(FROM_HERE);

  ssl_.reset(SSL_new(context->ssl_ctx()));
  if (!ssl_ || !context->SetClientSocketForSSL(ssl_.get(), this))
    return ERR_UNEXPECTED;

  // SNI should only contain valid DNS hostnames, not IP addresses (see RFC
  // 6066, Section 3).
  //
  // TODO(rsleevi): Should this code allow hostnames that violate the LDH rule?
  // See https://crbug.com/496472 and https://crbug.com/496468 for discussion.
  IPAddress unused;
  if (!unused.AssignFromIPLiteral(host_and_port_.host()) &&
      !SSL_set_tlsext_host_name(ssl_.get(), host_and_port_.host().c_str())) {
    return ERR_UNEXPECTED;
  }

  if (!ssl_session_cache_shard_.empty()) {
    bssl::UniquePtr<SSL_SESSION> session =
        context->session_cache()->Lookup(GetSessionCacheKey());
    if (session)
      SSL_set_session(ssl_.get(), session.get());
  }

  transport_adapter_.reset(new SocketBIOAdapter(
      transport_->socket(), GetBufferSize("SSLBufferSizeRecv"),
      GetBufferSize("SSLBufferSizeSend"), this));
  BIO* transport_bio = transport_adapter_->bio();

  BIO_up_ref(transport_bio);  // SSL_set0_rbio takes ownership.
  SSL_set0_rbio(ssl_.get(), transport_bio);

  BIO_up_ref(transport_bio);  // SSL_set0_wbio takes ownership.
  SSL_set0_wbio(ssl_.get(), transport_bio);

  DCHECK_LT(SSL3_VERSION, ssl_config_.version_min);
  DCHECK_LT(SSL3_VERSION, ssl_config_.version_max);
  if (!SSL_set_min_proto_version(ssl_.get(), ssl_config_.version_min) ||
      !SSL_set_max_proto_version(ssl_.get(), ssl_config_.version_max)) {
    return ERR_UNEXPECTED;
  }

  switch (ssl_config_.tls13_variant) {
    case kTLS13VariantDraft:
      SSL_set_tls13_variant(ssl_.get(), tls13_default);
      break;
    case kTLS13VariantExperiment:
      SSL_set_tls13_variant(ssl_.get(), tls13_experiment);
      break;
    case kTLS13VariantExperiment2:
      SSL_set_tls13_variant(ssl_.get(), tls13_experiment2);
      break;
    case kTLS13VariantExperiment3:
      SSL_set_tls13_variant(ssl_.get(), tls13_experiment3);
      break;
  }

  // OpenSSL defaults some options to on, others to off. To avoid ambiguity,
  // set everything we care about to an absolute value.
  SslSetClearMask options;
  options.ConfigureFlag(SSL_OP_NO_COMPRESSION, true);

  // TODO(joth): Set this conditionally, see http://crbug.com/55410
  options.ConfigureFlag(SSL_OP_LEGACY_SERVER_CONNECT, true);

  SSL_set_options(ssl_.get(), options.set_mask);
  SSL_clear_options(ssl_.get(), options.clear_mask);

  // Same as above, this time for the SSL mode.
  SslSetClearMask mode;

  mode.ConfigureFlag(SSL_MODE_RELEASE_BUFFERS, true);
  mode.ConfigureFlag(SSL_MODE_CBC_RECORD_SPLITTING, true);

  mode.ConfigureFlag(SSL_MODE_ENABLE_FALSE_START,
                     ssl_config_.false_start_enabled);

  SSL_set_mode(ssl_.get(), mode.set_mask);
  SSL_clear_mode(ssl_.get(), mode.clear_mask);

  // Use BoringSSL defaults, but disable HMAC-SHA256 and HMAC-SHA384 ciphers
  // (note that SHA256 and SHA384 only select legacy CBC ciphers).
  // Additionally disable HMAC-SHA1 ciphers in ECDSA. These are the remaining
  // CBC-mode ECDSA ciphers.
  std::string command("ALL:!SHA256:!SHA384:!aPSK:!ECDSA+SHA1");

  if (ssl_config_.require_ecdhe)
    command.append(":!kRSA");

  // Remove any disabled ciphers.
  for (uint16_t id : ssl_config_.disabled_cipher_suites) {
    const SSL_CIPHER* cipher = SSL_get_cipher_by_value(id);
    if (cipher) {
      command.append(":!");
      command.append(SSL_CIPHER_get_name(cipher));
    }
  }

  if (!SSL_set_strict_cipher_list(ssl_.get(), command.c_str())) {
    LOG(ERROR) << "SSL_set_cipher_list('" << command << "') failed";
    return ERR_UNEXPECTED;
  }

  // TLS channel ids.
  if (IsChannelIDEnabled()) {
    SSL_enable_tls_channel_id(ssl_.get());
  }

  if (!ssl_config_.alpn_protos.empty()) {
    std::vector<uint8_t> wire_protos =
        SerializeNextProtos(ssl_config_.alpn_protos);
    SSL_set_alpn_protos(ssl_.get(),
                        wire_protos.empty() ? NULL : &wire_protos[0],
                        wire_protos.size());
  }

  if (ssl_config_.signed_cert_timestamps_enabled) {
    SSL_enable_signed_cert_timestamps(ssl_.get());
    SSL_enable_ocsp_stapling(ssl_.get());
  }

  if (cert_verifier_->SupportsOCSPStapling())
    SSL_enable_ocsp_stapling(ssl_.get());

  // Configure BoringSSL to allow renegotiations. Once the initial handshake
  // completes, if renegotiations are not allowed, the default reject value will
  // be restored. This is done in this order to permit a BoringSSL
  // optimization. See https://crbug.com/boringssl/123.
  SSL_set_renegotiate_mode(ssl_.get(), ssl_renegotiate_freely);

  return OK;
}

void SSLClientSocketImpl::DoReadCallback(int rv) {
  // Since Run may result in Read being called, clear |user_read_callback_|
  // up front.
  if (rv > 0)
    was_ever_used_ = true;
  user_read_buf_ = nullptr;
  user_read_buf_len_ = 0;
  base::ResetAndReturn(&user_read_callback_).Run(rv);
}

void SSLClientSocketImpl::DoWriteCallback(int rv) {
  // Since Run may result in Write being called, clear |user_write_callback_|
  // up front.
  if (rv > 0)
    was_ever_used_ = true;
  user_write_buf_ = NULL;
  user_write_buf_len_ = 0;
  base::ResetAndReturn(&user_write_callback_).Run(rv);
}

// TODO(cbentzel): Remove including "base/threading/thread_local.h" and
// g_first_run_completed once crbug.com/424386 is fixed.
base::LazyInstance<base::ThreadLocalBoolean>::Leaky g_first_run_completed =
    LAZY_INSTANCE_INITIALIZER;

int SSLClientSocketImpl::DoHandshake() {
  crypto::OpenSSLErrStackTracer err_tracer(FROM_HERE);

  int rv;

  // TODO(cbentzel): Leave only 1 call to SSL_do_handshake once crbug.com/424386
  // is fixed.
  if (ssl_config_.send_client_cert && ssl_config_.client_cert.get()) {
    rv = SSL_do_handshake(ssl_.get());
  } else {
    if (g_first_run_completed.Get().Get()) {
      rv = SSL_do_handshake(ssl_.get());
    } else {
      g_first_run_completed.Get().Set(true);
      rv = SSL_do_handshake(ssl_.get());
    }
  }

  int net_error = OK;
  if (rv <= 0) {
    int ssl_error = SSL_get_error(ssl_.get(), rv);
    if (ssl_error == SSL_ERROR_WANT_CHANNEL_ID_LOOKUP) {
      // The server supports channel ID. Stop to look one up before returning to
      // the handshake.
      next_handshake_state_ = STATE_CHANNEL_ID_LOOKUP;
      return OK;
    }
    if (ssl_error == SSL_ERROR_WANT_X509_LOOKUP &&
        !ssl_config_.send_client_cert) {
      return ERR_SSL_CLIENT_AUTH_CERT_NEEDED;
    }
    if (ssl_error == SSL_ERROR_WANT_PRIVATE_KEY_OPERATION) {
      DCHECK(ssl_config_.client_private_key);
      DCHECK_NE(kNoPendingResult, signature_result_);
      next_handshake_state_ = STATE_HANDSHAKE;
      return ERR_IO_PENDING;
    }

    OpenSSLErrorInfo error_info;
    net_error = MapLastOpenSSLError(ssl_error, err_tracer, &error_info);
    if (net_error == ERR_IO_PENDING) {
      // If not done, stay in this state
      next_handshake_state_ = STATE_HANDSHAKE;
      return ERR_IO_PENDING;
    }

    switch (net_error) {
      case ERR_CONNECTION_CLOSED:
        connect_error_details_ = SSLErrorDetails::kConnectionClosed;
        break;
      case ERR_CONNECTION_RESET:
        connect_error_details_ = SSLErrorDetails::kConnectionReset;
        break;
      case ERR_SSL_PROTOCOL_ERROR: {
        int lib = ERR_GET_LIB(error_info.error_code);
        int reason = ERR_GET_REASON(error_info.error_code);
        if (lib == ERR_LIB_SSL && reason == SSL_R_TLSV1_ALERT_ACCESS_DENIED) {
          connect_error_details_ = SSLErrorDetails::kAccessDeniedAlert;
        } else if (lib == ERR_LIB_SSL &&
                   reason == SSL_R_APPLICATION_DATA_INSTEAD_OF_HANDSHAKE) {
          connect_error_details_ =
              SSLErrorDetails::kApplicationDataInsteadOfHandshake;
        } else {
          connect_error_details_ = SSLErrorDetails::kProtocolError;
        }
        break;
      }
      case ERR_SSL_BAD_RECORD_MAC_ALERT:
        connect_error_details_ = SSLErrorDetails::kBadRecordMACAlert;
        break;
      case ERR_SSL_VERSION_OR_CIPHER_MISMATCH:
        connect_error_details_ = SSLErrorDetails::kVersionOrCipherMismatch;
        break;
      default:
        connect_error_details_ = SSLErrorDetails::kOther;
        break;
    }

    LOG(ERROR) << "handshake failed; returned " << rv << ", SSL error code "
               << ssl_error << ", net_error " << net_error;
    net_log_.AddEvent(
        NetLogEventType::SSL_HANDSHAKE_ERROR,
        CreateNetLogOpenSSLErrorCallback(net_error, ssl_error, error_info));
  }

  next_handshake_state_ = STATE_HANDSHAKE_COMPLETE;
  return net_error;
}

int SSLClientSocketImpl::DoHandshakeComplete(int result) {
  if (result < 0)
    return result;

  if (ssl_config_.version_interference_probe) {
    DCHECK_LT(ssl_config_.version_max, TLS1_3_VERSION);
    return ERR_SSL_VERSION_INTERFERENCE;
  }

  if (!ssl_session_cache_shard_.empty()) {
    SSLContext::GetInstance()->session_cache()->ResetLookupCount(
        GetSessionCacheKey());
  }

  // Check that if token binding was negotiated, then extended master secret
  // and renegotiation indication must also be negotiated.
  if (tb_was_negotiated_ &&
      !(SSL_get_extms_support(ssl_.get()) &&
        SSL_get_secure_renegotiation_support(ssl_.get()))) {
    return ERR_SSL_PROTOCOL_ERROR;
  }

  const uint8_t* alpn_proto = NULL;
  unsigned alpn_len = 0;
  SSL_get0_alpn_selected(ssl_.get(), &alpn_proto, &alpn_len);
  if (alpn_len > 0) {
    base::StringPiece proto(reinterpret_cast<const char*>(alpn_proto),
                            alpn_len);
    negotiated_protocol_ = NextProtoFromString(proto);
  }

  RecordNegotiatedProtocol();
  RecordChannelIDSupport();

  const uint8_t* ocsp_response_raw;
  size_t ocsp_response_len;
  SSL_get0_ocsp_response(ssl_.get(), &ocsp_response_raw, &ocsp_response_len);
  set_stapled_ocsp_response_received(ocsp_response_len != 0);
  UMA_HISTOGRAM_BOOLEAN("Net.OCSPResponseStapled", ocsp_response_len != 0);

  const uint8_t* sct_list;
  size_t sct_list_len;
  SSL_get0_signed_cert_timestamp_list(ssl_.get(), &sct_list, &sct_list_len);
  set_signed_cert_timestamps_received(sct_list_len != 0);

  if (!IsRenegotiationAllowed())
    SSL_set_renegotiate_mode(ssl_.get(), ssl_renegotiate_never);

  uint16_t signature_algorithm = SSL_get_peer_signature_algorithm(ssl_.get());
  if (signature_algorithm != 0) {
    UMA_HISTOGRAM_SPARSE_SLOWLY("Net.SSLSignatureAlgorithm",
                                signature_algorithm);
  }

  // Verify the certificate.
  next_handshake_state_ = STATE_VERIFY_CERT;
  return OK;
}

int SSLClientSocketImpl::DoChannelIDLookup() {
  NetLogParametersCallback callback = base::Bind(
      &NetLogChannelIDLookupCallback, base::Unretained(channel_id_service_));
  net_log_.BeginEvent(NetLogEventType::SSL_GET_CHANNEL_ID, callback);
  next_handshake_state_ = STATE_CHANNEL_ID_LOOKUP_COMPLETE;
  return channel_id_service_->GetOrCreateChannelID(
      host_and_port_.host(), &channel_id_key_,
      base::Bind(&SSLClientSocketImpl::OnHandshakeIOComplete,
                 base::Unretained(this)),
      &channel_id_request_);
}

int SSLClientSocketImpl::DoChannelIDLookupComplete(int result) {
  net_log_.EndEvent(NetLogEventType::SSL_GET_CHANNEL_ID,
                    base::Bind(&NetLogChannelIDLookupCompleteCallback,
                               channel_id_key_.get(), result));
  if (result < 0)
    return result;

  // Hand the key to OpenSSL. Check for error in case OpenSSL rejects the key
  // type.
  DCHECK(channel_id_key_);
  crypto::OpenSSLErrStackTracer err_tracer(FROM_HERE);
  if (!SSL_set1_tls_channel_id(ssl_.get(), channel_id_key_->key())) {
    LOG(ERROR) << "Failed to set Channel ID.";
    return ERR_FAILED;
  }

  // Return to the handshake.
  channel_id_sent_ = true;
  next_handshake_state_ = STATE_HANDSHAKE;
  return OK;
}

int SSLClientSocketImpl::DoVerifyCert(int result) {
  DCHECK(start_cert_verification_time_.is_null());

  server_cert_ = x509_util::CreateX509CertificateFromBuffers(
      SSL_get0_peer_certificates(ssl_.get()));

  // OpenSSL decoded the certificate, but the X509Certificate implementation
  // could not. This is treated as a fatal SSL-level protocol error rather than
  // a certificate error. See https://crbug.com/91341.
  if (!server_cert_)
    return ERR_SSL_SERVER_CERT_BAD_FORMAT;

  net_log_.AddEvent(NetLogEventType::SSL_CERTIFICATES_RECEIVED,
                    base::Bind(&NetLogX509CertificateCallback,
                               base::Unretained(server_cert_.get())));

  next_handshake_state_ = STATE_VERIFY_CERT_COMPLETE;

  // If the certificate is bad and has been previously accepted, use
  // the previous status and bypass the error.
  CertStatus cert_status;
  if (ssl_config_.IsAllowedBadCert(server_cert_.get(), &cert_status)) {
    server_cert_verify_result_.Reset();
    server_cert_verify_result_.cert_status = cert_status;
    server_cert_verify_result_.verified_cert = server_cert_;
    return OK;
  }

  start_cert_verification_time_ = base::TimeTicks::Now();

  const uint8_t* ocsp_response_raw;
  size_t ocsp_response_len;
  SSL_get0_ocsp_response(ssl_.get(), &ocsp_response_raw, &ocsp_response_len);
  base::StringPiece ocsp_response(
      reinterpret_cast<const char*>(ocsp_response_raw), ocsp_response_len);

  return cert_verifier_->Verify(
      CertVerifier::RequestParams(server_cert_, host_and_port_.host(),
                                  ssl_config_.GetCertVerifyFlags(),
                                  ocsp_response.as_string(), CertificateList()),
      // TODO(davidben): Route the CRLSet through SSLConfig so
      // SSLClientSocket doesn't depend on SSLConfigService.
      SSLConfigService::GetCRLSet().get(), &server_cert_verify_result_,
      base::Bind(&SSLClientSocketImpl::OnHandshakeIOComplete,
                 base::Unretained(this)),
      &cert_verifier_request_, net_log_);
}

int SSLClientSocketImpl::DoVerifyCertComplete(int result) {
  cert_verifier_request_.reset();

  if (!start_cert_verification_time_.is_null()) {
    base::TimeDelta verify_time =
        base::TimeTicks::Now() - start_cert_verification_time_;
    if (result == OK) {
      UMA_HISTOGRAM_TIMES("Net.SSLCertVerificationTime", verify_time);
    } else {
      UMA_HISTOGRAM_TIMES("Net.SSLCertVerificationTimeError", verify_time);
    }
  }

  // If the connection was good, check HPKP and CT status simultaneously,
  // but prefer to treat the HPKP error as more serious, if there was one.
  const CertStatus cert_status = server_cert_verify_result_.cert_status;
  if ((result == OK ||
       (IsCertificateError(result) && IsCertStatusMinorError(cert_status)))) {
    int ct_result = VerifyCT();
    TransportSecurityState::PKPStatus pin_validity =
        transport_security_state_->CheckPublicKeyPins(
            host_and_port_, server_cert_verify_result_.is_issued_by_known_root,
            server_cert_verify_result_.public_key_hashes, server_cert_.get(),
            server_cert_verify_result_.verified_cert.get(),
            TransportSecurityState::ENABLE_PIN_REPORTS, &pinning_failure_log_);
    switch (pin_validity) {
      case TransportSecurityState::PKPStatus::VIOLATED:
        server_cert_verify_result_.cert_status |=
            CERT_STATUS_PINNED_KEY_MISSING;
        result = ERR_SSL_PINNED_KEY_NOT_IN_CERT_CHAIN;
        break;
      case TransportSecurityState::PKPStatus::BYPASSED:
        pkp_bypassed_ = true;
      // Fall through.
      case TransportSecurityState::PKPStatus::OK:
        // Do nothing.
        break;
    }
    if (result != ERR_SSL_PINNED_KEY_NOT_IN_CERT_CHAIN && ct_result != OK)
      result = ct_result;
  }

  if (result == OK) {
    DCHECK(!certificate_verified_);
    certificate_verified_ = true;
    MaybeCacheSession();
    SSLInfo ssl_info;
    bool ok = GetSSLInfo(&ssl_info);
    DCHECK(ok);

    const uint8_t* ocsp_response_raw;
    size_t ocsp_response_len;
    SSL_get0_ocsp_response(ssl_.get(), &ocsp_response_raw, &ocsp_response_len);
    base::StringPiece ocsp_response(
        reinterpret_cast<const char*>(ocsp_response_raw), ocsp_response_len);

    transport_security_state_->CheckExpectStaple(host_and_port_, ssl_info,
                                                 ocsp_response);
  }

  completed_connect_ = true;
  // Exit DoHandshakeLoop and return the result to the caller to Connect.
  DCHECK_EQ(STATE_NONE, next_handshake_state_);
  return result;
}

void SSLClientSocketImpl::DoConnectCallback(int rv) {
  if (!user_connect_callback_.is_null()) {
    CompletionCallback c = user_connect_callback_;
    user_connect_callback_.Reset();
    c.Run(rv > OK ? OK : rv);
  }
}

void SSLClientSocketImpl::OnHandshakeIOComplete(int result) {
  int rv = DoHandshakeLoop(result);
  if (rv != ERR_IO_PENDING) {
    LogConnectEndEvent(rv);
    DoConnectCallback(rv);
  }
}

int SSLClientSocketImpl::DoHandshakeLoop(int last_io_result) {
  TRACE_EVENT0(kNetTracingCategory, "SSLClientSocketImpl::DoHandshakeLoop");
  int rv = last_io_result;
  do {
    // Default to STATE_NONE for next state.
    // (This is a quirk carried over from the windows
    // implementation.  It makes reading the logs a bit harder.)
    // State handlers can and often do call GotoState just
    // to stay in the current state.
    State state = next_handshake_state_;
    next_handshake_state_ = STATE_NONE;
    switch (state) {
      case STATE_HANDSHAKE:
        rv = DoHandshake();
        break;
      case STATE_HANDSHAKE_COMPLETE:
        rv = DoHandshakeComplete(rv);
        break;
      case STATE_CHANNEL_ID_LOOKUP:
        DCHECK_EQ(OK, rv);
        rv = DoChannelIDLookup();
        break;
      case STATE_CHANNEL_ID_LOOKUP_COMPLETE:
        rv = DoChannelIDLookupComplete(rv);
        break;
      case STATE_VERIFY_CERT:
        DCHECK_EQ(OK, rv);
        rv = DoVerifyCert(rv);
        break;
      case STATE_VERIFY_CERT_COMPLETE:
        rv = DoVerifyCertComplete(rv);
        break;
      case STATE_NONE:
      default:
        rv = ERR_UNEXPECTED;
        NOTREACHED() << "unexpected state" << state;
        break;
    }
  } while (rv != ERR_IO_PENDING && next_handshake_state_ != STATE_NONE);
  return rv;
}

int SSLClientSocketImpl::DoPayloadRead(IOBuffer* buf, int buf_len) {
  crypto::OpenSSLErrStackTracer err_tracer(FROM_HERE);

  DCHECK_LT(0, buf_len);
  DCHECK(buf);

  int rv;
  if (pending_read_error_ != kNoPendingResult) {
    rv = pending_read_error_;
    pending_read_error_ = kNoPendingResult;
    if (rv == 0) {
      net_log_.AddByteTransferEvent(NetLogEventType::SSL_SOCKET_BYTES_RECEIVED,
                                    rv, buf->data());
    } else {
      net_log_.AddEvent(
          NetLogEventType::SSL_READ_ERROR,
          CreateNetLogOpenSSLErrorCallback(rv, pending_read_ssl_error_,
                                           pending_read_error_info_));
    }
    pending_read_ssl_error_ = SSL_ERROR_NONE;
    pending_read_error_info_ = OpenSSLErrorInfo();
    return rv;
  }

  int total_bytes_read = 0;
  int ssl_ret;
  do {
    ssl_ret = SSL_read(ssl_.get(), buf->data() + total_bytes_read,
                       buf_len - total_bytes_read);
    if (ssl_ret > 0)
      total_bytes_read += ssl_ret;
    // Continue processing records as long as there is more data available
    // synchronously.
  } while (total_bytes_read < buf_len && ssl_ret > 0 &&
           transport_adapter_->HasPendingReadData());

  // Although only the final SSL_read call may have failed, the failure needs to
  // processed immediately, while the information still available in OpenSSL's
  // error queue.
  if (ssl_ret <= 0) {
    // A zero return from SSL_read may mean any of:
    // - The underlying BIO_read returned 0.
    // - The peer sent a close_notify.
    // - Any arbitrary error. https://crbug.com/466303
    //
    // TransportReadComplete converts the first to an ERR_CONNECTION_CLOSED
    // error, so it does not occur. The second and third are distinguished by
    // SSL_ERROR_ZERO_RETURN.
    pending_read_ssl_error_ = SSL_get_error(ssl_.get(), ssl_ret);
    if (pending_read_ssl_error_ == SSL_ERROR_ZERO_RETURN) {
      pending_read_error_ = 0;
    } else if (pending_read_ssl_error_ == SSL_ERROR_WANT_X509_LOOKUP &&
               !ssl_config_.send_client_cert) {
      pending_read_error_ = ERR_SSL_CLIENT_AUTH_CERT_NEEDED;
    } else if (pending_read_ssl_error_ ==
               SSL_ERROR_WANT_PRIVATE_KEY_OPERATION) {
      DCHECK(ssl_config_.client_private_key);
      DCHECK_NE(kNoPendingResult, signature_result_);
      pending_read_error_ = ERR_IO_PENDING;
    } else {
      pending_read_error_ = MapLastOpenSSLError(
          pending_read_ssl_error_, err_tracer, &pending_read_error_info_);
    }

    // Many servers do not reliably send a close_notify alert when shutting down
    // a connection, and instead terminate the TCP connection. This is reported
    // as ERR_CONNECTION_CLOSED. Because of this, map the unclean shutdown to a
    // graceful EOF, instead of treating it as an error as it should be.
    if (pending_read_error_ == ERR_CONNECTION_CLOSED)
      pending_read_error_ = 0;
  }

  if (total_bytes_read > 0) {
    // Return any bytes read to the caller. The error will be deferred to the
    // next call of DoPayloadRead.
    rv = total_bytes_read;

    // Do not treat insufficient data as an error to return in the next call to
    // DoPayloadRead() - instead, let the call fall through to check SSL_read()
    // again. The transport may have data available by then.
    if (pending_read_error_ == ERR_IO_PENDING)
      pending_read_error_ = kNoPendingResult;
  } else {
    // No bytes were returned. Return the pending read error immediately.
    DCHECK_NE(kNoPendingResult, pending_read_error_);
    rv = pending_read_error_;
    pending_read_error_ = kNoPendingResult;
  }

  if (rv >= 0) {
    net_log_.AddByteTransferEvent(NetLogEventType::SSL_SOCKET_BYTES_RECEIVED,
                                  rv, buf->data());
  } else if (rv != ERR_IO_PENDING) {
    net_log_.AddEvent(
        NetLogEventType::SSL_READ_ERROR,
        CreateNetLogOpenSSLErrorCallback(rv, pending_read_ssl_error_,
                                         pending_read_error_info_));
    pending_read_ssl_error_ = SSL_ERROR_NONE;
    pending_read_error_info_ = OpenSSLErrorInfo();
  }
  return rv;
}

int SSLClientSocketImpl::DoPayloadWrite() {
  crypto::OpenSSLErrStackTracer err_tracer(FROM_HERE);
  int rv = SSL_write(ssl_.get(), user_write_buf_->data(), user_write_buf_len_);

  if (rv >= 0) {
    net_log_.AddByteTransferEvent(NetLogEventType::SSL_SOCKET_BYTES_SENT, rv,
                                  user_write_buf_->data());
    return rv;
  }

  int ssl_error = SSL_get_error(ssl_.get(), rv);
  if (ssl_error == SSL_ERROR_WANT_PRIVATE_KEY_OPERATION)
    return ERR_IO_PENDING;
  OpenSSLErrorInfo error_info;
  int net_error = MapLastOpenSSLError(ssl_error, err_tracer, &error_info);

  if (net_error != ERR_IO_PENDING) {
    net_log_.AddEvent(
        NetLogEventType::SSL_WRITE_ERROR,
        CreateNetLogOpenSSLErrorCallback(net_error, ssl_error, error_info));
  }
  return net_error;
}

void SSLClientSocketImpl::RetryAllOperations() {
  // SSL_do_handshake, SSL_read, and SSL_write may all be retried when blocked,
  // so retry all operations for simplicity. (Otherwise, SSL_get_error for each
  // operation may be remembered to retry only the blocked ones.)

  if (next_handshake_state_ == STATE_HANDSHAKE) {
    // In handshake phase. The parameter to OnHandshakeIOComplete is unused.
    OnHandshakeIOComplete(OK);
    return;
  }

  int rv_read = ERR_IO_PENDING;
  int rv_write = ERR_IO_PENDING;
  if (user_read_buf_) {
    rv_read = DoPayloadRead(user_read_buf_.get(), user_read_buf_len_);
  } else if (!user_read_callback_.is_null()) {
    // ReadIfReady() is called by the user. Skip DoPayloadRead() and just let
    // the user know that read can be retried.
    rv_read = OK;
  }

  if (user_write_buf_)
    rv_write = DoPayloadWrite();

  // Performing the Read callback may cause |this| to be deleted. If this
  // happens, the Write callback should not be invoked. Guard against this by
  // holding a WeakPtr to |this| and ensuring it's still valid.
  base::WeakPtr<SSLClientSocketImpl> guard(weak_factory_.GetWeakPtr());
  if (rv_read != ERR_IO_PENDING)
    DoReadCallback(rv_read);

  if (!guard.get())
    return;

  if (rv_write != ERR_IO_PENDING)
    DoWriteCallback(rv_write);
}

int SSLClientSocketImpl::VerifyCT() {
  const uint8_t* sct_list_raw;
  size_t sct_list_len;
  SSL_get0_signed_cert_timestamp_list(ssl_.get(), &sct_list_raw, &sct_list_len);
  base::StringPiece sct_list(reinterpret_cast<const char*>(sct_list_raw),
                             sct_list_len);

  const uint8_t* ocsp_response_raw;
  size_t ocsp_response_len;
  SSL_get0_ocsp_response(ssl_.get(), &ocsp_response_raw, &ocsp_response_len);
  base::StringPiece ocsp_response(
      reinterpret_cast<const char*>(ocsp_response_raw), ocsp_response_len);

  // Note that this is a completely synchronous operation: The CT Log Verifier
  // gets all the data it needs for SCT verification and does not do any
  // external communication.
  cert_transparency_verifier_->Verify(
      server_cert_verify_result_.verified_cert.get(), ocsp_response, sct_list,
      &ct_verify_result_.scts, net_log_);

  ct_verify_result_.ct_policies_applied = true;

  SCTList verified_scts =
      ct::SCTsMatchingStatus(ct_verify_result_.scts, ct::SCT_STATUS_OK);

  ct_verify_result_.cert_policy_compliance =
      policy_enforcer_->DoesConformToCertPolicy(
          server_cert_verify_result_.verified_cert.get(), verified_scts,
          net_log_);
  if ((server_cert_verify_result_.cert_status & CERT_STATUS_IS_EV) &&
      (ct_verify_result_.cert_policy_compliance !=
       ct::CertPolicyCompliance::CERT_POLICY_COMPLIES_VIA_SCTS)) {
    server_cert_verify_result_.cert_status |= CERT_STATUS_CT_COMPLIANCE_FAILED;
    server_cert_verify_result_.cert_status &= ~CERT_STATUS_IS_EV;
  }

  UMA_HISTOGRAM_ENUMERATION(
      "Net.CertificateTransparency.ConnectionComplianceStatus.SSL",
      ct_verify_result_.cert_policy_compliance,
      ct::CertPolicyCompliance::CERT_POLICY_MAX);

  if (transport_security_state_->CheckCTRequirements(
          host_and_port_, server_cert_verify_result_.is_issued_by_known_root,
          server_cert_verify_result_.public_key_hashes,
          server_cert_verify_result_.verified_cert.get(), server_cert_.get(),
          ct_verify_result_.scts,
          TransportSecurityState::ENABLE_EXPECT_CT_REPORTS,
          ct_verify_result_.cert_policy_compliance) !=
      TransportSecurityState::CT_REQUIREMENTS_MET) {
    server_cert_verify_result_.cert_status |=
        CERT_STATUS_CERTIFICATE_TRANSPARENCY_REQUIRED;
    return ERR_CERTIFICATE_TRANSPARENCY_REQUIRED;
  }

  return OK;
}

int SSLClientSocketImpl::ClientCertRequestCallback(SSL* ssl) {
  DCHECK(ssl == ssl_.get());

  net_log_.AddEvent(NetLogEventType::SSL_CLIENT_CERT_REQUESTED);
  certificate_requested_ = true;

  // Clear any currently configured certificates.
  SSL_certs_clear(ssl_.get());

#if defined(OS_IOS)
  // TODO(droger): Support client auth on iOS. See http://crbug.com/145954).
  LOG(WARNING) << "Client auth is not supported";
#else   // !defined(OS_IOS)
  if (!ssl_config_.send_client_cert) {
    // First pass: we know that a client certificate is needed, but we do not
    // have one at hand. Suspend the handshake. SSL_get_error will return
    // SSL_ERROR_WANT_X509_LOOKUP.
    return -1;
  }

  // Second pass: a client certificate should have been selected.
  if (ssl_config_.client_cert.get()) {
    if (!ssl_config_.client_private_key) {
      // The caller supplied a null private key. Fail the handshake and surface
      // an appropriate error to the caller.
      LOG(WARNING) << "Client cert found without private key";
      OpenSSLPutNetError(FROM_HERE, ERR_SSL_CLIENT_AUTH_CERT_NO_PRIVATE_KEY);
      return -1;
    }

    if (!SetSSLChainAndKey(ssl_.get(), ssl_config_.client_cert.get(), nullptr,
                           &SSLContext::kPrivateKeyMethod)) {
      OpenSSLPutNetError(FROM_HERE, ERR_SSL_CLIENT_AUTH_CERT_BAD_FORMAT);
      return -1;
    }

    std::vector<SSLPrivateKey::Hash> digest_prefs =
        ssl_config_.client_private_key->GetDigestPreferences();

    size_t digests_len = digest_prefs.size();
    std::vector<int> digests;
    for (size_t i = 0; i < digests_len; i++) {
      switch (digest_prefs[i]) {
        case SSLPrivateKey::Hash::SHA1:
          digests.push_back(NID_sha1);
          break;
        case SSLPrivateKey::Hash::SHA256:
          digests.push_back(NID_sha256);
          break;
        case SSLPrivateKey::Hash::SHA384:
          digests.push_back(NID_sha384);
          break;
        case SSLPrivateKey::Hash::SHA512:
          digests.push_back(NID_sha512);
          break;
        case SSLPrivateKey::Hash::MD5_SHA1:
          // MD5-SHA1 is not used in TLS 1.2.
          break;
      }
    }

    SSL_set_private_key_digest_prefs(ssl_.get(), digests.data(),
                                     digests.size());

    net_log_.AddEvent(
        NetLogEventType::SSL_CLIENT_CERT_PROVIDED,
        NetLog::IntCallback(
            "cert_count",
            1 + ssl_config_.client_cert->GetIntermediateCertificates().size()));
    return 1;
  }
#endif  // defined(OS_IOS)

  // Send no client certificate.
  net_log_.AddEvent(NetLogEventType::SSL_CLIENT_CERT_PROVIDED,
                    NetLog::IntCallback("cert_count", 0));
  return 1;
}

void SSLClientSocketImpl::MaybeCacheSession() {
  // Only cache the session once both a new session has been established and the
  // certificate has been verified. Due to False Start, these events may happen
  // in either order.
  if (!pending_session_ || !certificate_verified_ ||
      ssl_session_cache_shard_.empty()) {
    return;
  }

  SSLContext::GetInstance()->session_cache()->Insert(GetSessionCacheKey(),
                                                     pending_session_.get());
  pending_session_ = nullptr;
}

int SSLClientSocketImpl::NewSessionCallback(SSL_SESSION* session) {
  if (ssl_session_cache_shard_.empty())
    return 0;

  // OpenSSL passes a reference to |session|.
  pending_session_.reset(session);
  MaybeCacheSession();
  return 1;
}

void SSLClientSocketImpl::AddCTInfoToSSLInfo(SSLInfo* ssl_info) const {
  ssl_info->UpdateCertificateTransparencyInfo(ct_verify_result_);
}

std::string SSLClientSocketImpl::GetSessionCacheKey() const {
  // If there is no session cache shard configured, disable session
  // caching. GetSessionCacheKey may not be called. When
  // https://crbug.com/458365 is fixed, this check will not be needed.
  DCHECK(!ssl_session_cache_shard_.empty());

  std::string result = host_and_port_.ToString();
  result.push_back('/');
  result.append(ssl_session_cache_shard_);

  result.push_back('/');
  result.push_back(ssl_config_.channel_id_enabled ? '1' : '0');
  result.push_back(ssl_config_.version_interference_probe ? '1' : '0');
  return result;
}

bool SSLClientSocketImpl::IsRenegotiationAllowed() const {
  if (tb_was_negotiated_)
    return false;

  if (negotiated_protocol_ == kProtoUnknown)
    return ssl_config_.renego_allowed_default;

  for (NextProto allowed : ssl_config_.renego_allowed_for_protos) {
    if (negotiated_protocol_ == allowed)
      return true;
  }
  return false;
}

ssl_private_key_result_t SSLClientSocketImpl::PrivateKeySignDigestCallback(
    uint8_t* out,
    size_t* out_len,
    size_t max_out,
    const EVP_MD* md,
    const uint8_t* in,
    size_t in_len) {
  DCHECK_EQ(kNoPendingResult, signature_result_);
  DCHECK(signature_.empty());
  DCHECK(ssl_config_.client_private_key);

  SSLPrivateKey::Hash hash;
  if (!EVP_MDToPrivateKeyHash(md, &hash)) {
    OpenSSLPutNetError(FROM_HERE, ERR_SSL_CLIENT_AUTH_SIGNATURE_FAILED);
    return ssl_private_key_failure;
  }

  net_log_.BeginEvent(NetLogEventType::SSL_PRIVATE_KEY_OP,
                      base::Bind(&NetLogPrivateKeyOperationCallback, hash));

  signature_result_ = ERR_IO_PENDING;
  ssl_config_.client_private_key->SignDigest(
      hash, base::StringPiece(reinterpret_cast<const char*>(in), in_len),
      base::Bind(&SSLClientSocketImpl::OnPrivateKeyComplete,
                 weak_factory_.GetWeakPtr()));
  return ssl_private_key_retry;
}

ssl_private_key_result_t SSLClientSocketImpl::PrivateKeyCompleteCallback(
    uint8_t* out,
    size_t* out_len,
    size_t max_out) {
  DCHECK_NE(kNoPendingResult, signature_result_);
  DCHECK(ssl_config_.client_private_key);

  if (signature_result_ == ERR_IO_PENDING)
    return ssl_private_key_retry;
  if (signature_result_ != OK) {
    OpenSSLPutNetError(FROM_HERE, signature_result_);
    return ssl_private_key_failure;
  }
  if (signature_.size() > max_out) {
    OpenSSLPutNetError(FROM_HERE, ERR_SSL_CLIENT_AUTH_SIGNATURE_FAILED);
    return ssl_private_key_failure;
  }
  memcpy(out, signature_.data(), signature_.size());
  *out_len = signature_.size();
  signature_.clear();
  return ssl_private_key_success;
}

void SSLClientSocketImpl::OnPrivateKeyComplete(
    Error error,
    const std::vector<uint8_t>& signature) {
  DCHECK_EQ(ERR_IO_PENDING, signature_result_);
  DCHECK(signature_.empty());
  DCHECK(ssl_config_.client_private_key);

  net_log_.EndEventWithNetErrorCode(NetLogEventType::SSL_PRIVATE_KEY_OP, error);

  signature_result_ = error;
  if (signature_result_ == OK)
    signature_ = signature;

  // During a renegotiation, either Read or Write calls may be blocked on an
  // asynchronous private key operation.
  RetryAllOperations();
}

void SSLClientSocketImpl::MessageCallback(int is_write,
                                          int content_type,
                                          const void* buf,
                                          size_t len) {
  switch (content_type) {
    case SSL3_RT_ALERT:
      net_log_.AddEvent(is_write ? NetLogEventType::SSL_ALERT_SENT
                                 : NetLogEventType::SSL_ALERT_RECEIVED,
                        base::Bind(&NetLogSSLAlertCallback, buf, len));
      break;
    case SSL3_RT_HANDSHAKE:
      net_log_.AddEvent(
          is_write ? NetLogEventType::SSL_HANDSHAKE_MESSAGE_SENT
                   : NetLogEventType::SSL_HANDSHAKE_MESSAGE_RECEIVED,
          base::Bind(&NetLogSSLMessageCallback, !!is_write, buf, len));
      break;
    case SSL3_RT_HEADER: {
      if (is_write)
        return;
      if (len != 5) {
        NOTREACHED();
        return;
      }
      const uint8_t* buf_bytes = reinterpret_cast<const uint8_t*>(buf);
      uint16_t record_len = (uint16_t(buf_bytes[3]) << 8) | buf_bytes[4];
      // See RFC 5246 section 6.2.3 for the maximum record size in TLS.
      UMA_HISTOGRAM_CUSTOM_COUNTS("Net.SSLRecordSizeRead", record_len, 1,
                                  16384 + 2048, 50);
    }
    default:
      return;
  }
}

int SSLClientSocketImpl::TokenBindingAdd(const uint8_t** out,
                                         size_t* out_len,
                                         int* out_alert_value) {
  if (ssl_config_.token_binding_params.empty()) {
    return 0;
  }
  bssl::ScopedCBB output;
  CBB parameters_list;
  if (!CBB_init(output.get(), 7) ||
      !CBB_add_u8(output.get(), kTbProtocolVersionMajor) ||
      !CBB_add_u8(output.get(), kTbProtocolVersionMinor) ||
      !CBB_add_u8_length_prefixed(output.get(), &parameters_list)) {
    *out_alert_value = SSL_AD_INTERNAL_ERROR;
    return -1;
  }
  for (size_t i = 0; i < ssl_config_.token_binding_params.size(); ++i) {
    if (!CBB_add_u8(&parameters_list, ssl_config_.token_binding_params[i])) {
      *out_alert_value = SSL_AD_INTERNAL_ERROR;
      return -1;
    }
  }
  // |*out| will be freed by TokenBindingFreeCallback.
  if (!CBB_finish(output.get(), const_cast<uint8_t**>(out), out_len)) {
    *out_alert_value = SSL_AD_INTERNAL_ERROR;
    return -1;
  }

  return 1;
}

int SSLClientSocketImpl::TokenBindingParse(const uint8_t* contents,
                                           size_t contents_len,
                                           int* out_alert_value) {
  if (completed_connect_) {
    // Token Binding may only be negotiated on the initial handshake.
    *out_alert_value = SSL_AD_ILLEGAL_PARAMETER;
    return 0;
  }

  CBS extension;
  CBS_init(&extension, contents, contents_len);

  CBS parameters_list;
  uint8_t version_major, version_minor, param;
  if (!CBS_get_u8(&extension, &version_major) ||
      !CBS_get_u8(&extension, &version_minor) ||
      !CBS_get_u8_length_prefixed(&extension, &parameters_list) ||
      !CBS_get_u8(&parameters_list, &param) || CBS_len(&parameters_list) > 0 ||
      CBS_len(&extension) > 0) {
    *out_alert_value = SSL_AD_DECODE_ERROR;
    return 0;
  }
  // The server-negotiated version must be less than or equal to our version.
  if (version_major > kTbProtocolVersionMajor ||
      (version_minor > kTbProtocolVersionMinor &&
       version_major == kTbProtocolVersionMajor)) {
    *out_alert_value = SSL_AD_ILLEGAL_PARAMETER;
    return 0;
  }
  // If the version the server negotiated is older than we support, don't fail
  // parsing the extension, but also don't set |negotiated_|.
  if (version_major < kTbMinProtocolVersionMajor ||
      (version_minor < kTbMinProtocolVersionMinor &&
       version_major == kTbMinProtocolVersionMajor)) {
    return 1;
  }

  for (size_t i = 0; i < ssl_config_.token_binding_params.size(); ++i) {
    if (param == ssl_config_.token_binding_params[i]) {
      tb_negotiated_param_ = ssl_config_.token_binding_params[i];
      tb_was_negotiated_ = true;
      return 1;
    }
  }

  *out_alert_value = SSL_AD_ILLEGAL_PARAMETER;
  return 0;
}

void SSLClientSocketImpl::LogConnectEndEvent(int rv) {
  if (rv != OK) {
    net_log_.EndEventWithNetErrorCode(NetLogEventType::SSL_CONNECT, rv);
    return;
  }

  net_log_.EndEvent(NetLogEventType::SSL_CONNECT,
                    base::Bind(&NetLogSSLInfoCallback, base::Unretained(this)));
}

void SSLClientSocketImpl::RecordNegotiatedProtocol() const {
  UMA_HISTOGRAM_ENUMERATION("Net.SSLNegotiatedAlpnProtocol",
                            negotiated_protocol_, kProtoLast + 1);
}

void SSLClientSocketImpl::RecordChannelIDSupport() const {
  // Since this enum is used for a histogram, do not change or re-use values.
  enum {
    DISABLED = 0,
    CLIENT_ONLY = 1,
    CLIENT_AND_SERVER = 2,
    // CLIENT_NO_ECC is unused now.
    // CLIENT_BAD_SYSTEM_TIME is unused now.
    CLIENT_BAD_SYSTEM_TIME = 4,
    CLIENT_NO_CHANNEL_ID_SERVICE = 5,
    CHANNEL_ID_USAGE_MAX
  } supported = DISABLED;
  if (channel_id_sent_) {
    supported = CLIENT_AND_SERVER;
  } else if (ssl_config_.channel_id_enabled) {
    if (!channel_id_service_)
      supported = CLIENT_NO_CHANNEL_ID_SERVICE;
    else
      supported = CLIENT_ONLY;
  }
  UMA_HISTOGRAM_ENUMERATION("DomainBoundCerts.Support", supported,
                            CHANNEL_ID_USAGE_MAX);
}

bool SSLClientSocketImpl::IsChannelIDEnabled() const {
  return ssl_config_.channel_id_enabled && channel_id_service_;
}

int SSLClientSocketImpl::MapLastOpenSSLError(
    int ssl_error,
    const crypto::OpenSSLErrStackTracer& tracer,
    OpenSSLErrorInfo* info) {
  int net_error = MapOpenSSLErrorWithDetails(ssl_error, tracer, info);

  if (ssl_error == SSL_ERROR_SSL &&
      ERR_GET_LIB(info->error_code) == ERR_LIB_SSL) {
    // TLS does not provide an alert for missing client certificates, so most
    // servers send a generic handshake_failure alert. Detect this case by
    // checking if we have received a CertificateRequest but sent no
    // certificate. See https://crbug.com/646567.
    if (ERR_GET_REASON(info->error_code) ==
            SSL_R_SSLV3_ALERT_HANDSHAKE_FAILURE &&
        certificate_requested_ && ssl_config_.send_client_cert &&
        !ssl_config_.client_cert) {
      net_error = ERR_BAD_SSL_CLIENT_AUTH_CERT;
    }

    // Per spec, access_denied is only for client-certificate-based access
    // control, but some buggy firewalls use it when blocking a page. To avoid a
    // confusing error, map it to a generic protocol error if no
    // CertificateRequest was sent. See https://crbug.com/630883.
    if (ERR_GET_REASON(info->error_code) == SSL_R_TLSV1_ALERT_ACCESS_DENIED &&
        !certificate_requested_) {
      net_error = ERR_SSL_PROTOCOL_ERROR;
    }
  }

  return net_error;
}

}  // namespace net
