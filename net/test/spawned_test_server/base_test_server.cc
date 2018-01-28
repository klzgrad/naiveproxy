// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/test/spawned_test_server/base_test_server.h"

#include <stdint.h>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/values.h"
#include "net/base/address_list.h"
#include "net/base/host_port_pair.h"
#include "net/base/net_errors.h"
#include "net/base/port_util.h"
#include "net/base/test_completion_callback.h"
#include "net/cert/test_root_certs.h"
#include "net/cert/x509_certificate.h"
#include "net/dns/host_resolver.h"
#include "net/log/net_log_with_source.h"
#include "url/gurl.h"

namespace net {

namespace {

std::string GetHostname(BaseTestServer::Type type,
                        const BaseTestServer::SSLOptions& options) {
  if (BaseTestServer::UsingSSL(type)) {
    if (options.server_certificate ==
            BaseTestServer::SSLOptions::CERT_MISMATCHED_NAME ||
        options.server_certificate ==
            BaseTestServer::SSLOptions::CERT_COMMON_NAME_IS_DOMAIN) {
      // For |CERT_MISMATCHED_NAME|, return a different hostname string
      // that resolves to the same hostname. For
      // |CERT_COMMON_NAME_IS_DOMAIN|, the certificate is issued for
      // "localhost" instead of "127.0.0.1".
      return "localhost";
    }
  }

  return "127.0.0.1";
}

std::string GetClientCertType(SSLClientCertType type) {
  switch (type) {
    case CLIENT_CERT_RSA_SIGN:
      return "rsa_sign";
    case CLIENT_CERT_ECDSA_SIGN:
      return "ecdsa_sign";
    default:
      NOTREACHED();
      return "";
  }
}

void GetKeyExchangesList(int key_exchange, base::ListValue* values) {
  if (key_exchange & BaseTestServer::SSLOptions::KEY_EXCHANGE_RSA)
    values->AppendString("rsa");
  if (key_exchange & BaseTestServer::SSLOptions::KEY_EXCHANGE_DHE_RSA)
    values->AppendString("dhe_rsa");
  if (key_exchange & BaseTestServer::SSLOptions::KEY_EXCHANGE_ECDHE_RSA)
    values->AppendString("ecdhe_rsa");
}

void GetCiphersList(int cipher, base::ListValue* values) {
  if (cipher & BaseTestServer::SSLOptions::BULK_CIPHER_RC4)
    values->AppendString("rc4");
  if (cipher & BaseTestServer::SSLOptions::BULK_CIPHER_AES128)
    values->AppendString("aes128");
  if (cipher & BaseTestServer::SSLOptions::BULK_CIPHER_AES256)
    values->AppendString("aes256");
  if (cipher & BaseTestServer::SSLOptions::BULK_CIPHER_3DES)
    values->AppendString("3des");
  if (cipher & BaseTestServer::SSLOptions::BULK_CIPHER_AES128GCM)
    values->AppendString("aes128gcm");
}

std::unique_ptr<base::Value> GetTLSIntoleranceType(
    BaseTestServer::SSLOptions::TLSIntoleranceType type) {
  switch (type) {
    case BaseTestServer::SSLOptions::TLS_INTOLERANCE_ALERT:
      return std::make_unique<base::Value>("alert");
    case BaseTestServer::SSLOptions::TLS_INTOLERANCE_CLOSE:
      return std::make_unique<base::Value>("close");
    case BaseTestServer::SSLOptions::TLS_INTOLERANCE_RESET:
      return std::make_unique<base::Value>("reset");
    default:
      NOTREACHED();
      return std::make_unique<base::Value>("");
  }
}

bool GetLocalCertificatesDir(const base::FilePath& certificates_dir,
                             base::FilePath* local_certificates_dir) {
  if (certificates_dir.IsAbsolute()) {
    *local_certificates_dir = certificates_dir;
    return true;
  }

  base::FilePath src_dir;
  if (!PathService::Get(base::DIR_SOURCE_ROOT, &src_dir))
    return false;

  *local_certificates_dir = src_dir.Append(certificates_dir);
  return true;
}

std::unique_ptr<base::ListValue> GetTokenBindingParams(
    std::vector<int> params) {
  std::unique_ptr<base::ListValue> values(new base::ListValue());
  for (int param : params) {
    values->AppendInteger(param);
  }
  return values;
}

std::string OCSPStatusToString(
    const BaseTestServer::SSLOptions::OCSPStatus& ocsp_status) {
  switch (ocsp_status) {
    case BaseTestServer::SSLOptions::OCSP_OK:
      return "ok";
    case BaseTestServer::SSLOptions::OCSP_REVOKED:
      return "revoked";
    case BaseTestServer::SSLOptions::OCSP_INVALID_RESPONSE:
      return "invalid";
    case BaseTestServer::SSLOptions::OCSP_UNAUTHORIZED:
      return "unauthorized";
    case BaseTestServer::SSLOptions::OCSP_UNKNOWN:
      return "unknown";
    case BaseTestServer::SSLOptions::OCSP_TRY_LATER:
      return "later";
    case BaseTestServer::SSLOptions::OCSP_INVALID_RESPONSE_DATA:
      return "invalid_data";
    case BaseTestServer::SSLOptions::OCSP_MISMATCHED_SERIAL:
      return "mismatched_serial";
  }
  NOTREACHED();
  return std::string();
}

std::string OCSPDateToString(
    const BaseTestServer::SSLOptions::OCSPDate& ocsp_date) {
  switch (ocsp_date) {
    case BaseTestServer::SSLOptions::OCSP_DATE_VALID:
      return "valid";
    case BaseTestServer::SSLOptions::OCSP_DATE_OLD:
      return "old";
    case BaseTestServer::SSLOptions::OCSP_DATE_EARLY:
      return "early";
    case BaseTestServer::SSLOptions::OCSP_DATE_LONG:
      return "long";
  }
  NOTREACHED();
  return std::string();
}

}  // namespace

BaseTestServer::SSLOptions::SSLOptions() {}
BaseTestServer::SSLOptions::SSLOptions(ServerCertificate cert)
    : server_certificate(cert) {}
BaseTestServer::SSLOptions::SSLOptions(const SSLOptions& other) = default;

BaseTestServer::SSLOptions::~SSLOptions() {}

base::FilePath BaseTestServer::SSLOptions::GetCertificateFile() const {
  switch (server_certificate) {
    case CERT_OK:
    case CERT_MISMATCHED_NAME:
      return base::FilePath(FILE_PATH_LITERAL("ok_cert.pem"));
    case CERT_COMMON_NAME_IS_DOMAIN:
      return base::FilePath(FILE_PATH_LITERAL("localhost_cert.pem"));
    case CERT_EXPIRED:
      return base::FilePath(FILE_PATH_LITERAL("expired_cert.pem"));
    case CERT_CHAIN_WRONG_ROOT:
      // This chain uses its own dedicated test root certificate to avoid
      // side-effects that may affect testing.
      return base::FilePath(FILE_PATH_LITERAL("redundant-server-chain.pem"));
    case CERT_BAD_VALIDITY:
      return base::FilePath(FILE_PATH_LITERAL("bad_validity.pem"));
    case CERT_AUTO:
    case CERT_AUTO_AIA_INTERMEDIATE:
      return base::FilePath();
    default:
      NOTREACHED();
  }
  return base::FilePath();
}

std::string BaseTestServer::SSLOptions::GetOCSPArgument() const {
  if (server_certificate != CERT_AUTO)
    return std::string();

  // |ocsp_responses| overrides when it is non-empty.
  if (!ocsp_responses.empty()) {
    std::string arg;
    for (size_t i = 0; i < ocsp_responses.size(); i++) {
      if (i != 0)
        arg += ":";
      arg += OCSPStatusToString(ocsp_responses[i].status);
    }
    return arg;
  }

  return OCSPStatusToString(ocsp_status);
}

std::string BaseTestServer::SSLOptions::GetOCSPDateArgument() const {
  if (server_certificate != CERT_AUTO)
    return std::string();

  if (!ocsp_responses.empty()) {
    std::string arg;
    for (size_t i = 0; i < ocsp_responses.size(); i++) {
      if (i != 0)
        arg += ":";
      arg += OCSPDateToString(ocsp_responses[i].date);
    }
    return arg;
  }

  return OCSPDateToString(ocsp_date);
}

std::string BaseTestServer::SSLOptions::GetOCSPProducedArgument() const {
  if (server_certificate != CERT_AUTO)
    return std::string();

  switch (ocsp_produced) {
    case OCSP_PRODUCED_VALID:
      return "valid";
    case OCSP_PRODUCED_BEFORE_CERT:
      return "before";
    case OCSP_PRODUCED_AFTER_CERT:
      return "after";
    default:
      NOTREACHED();
      return std::string();
  }
}

BaseTestServer::BaseTestServer(Type type) : type_(type) {
  Init(GetHostname(type, ssl_options_));
}

BaseTestServer::BaseTestServer(Type type, const SSLOptions& ssl_options)
    : ssl_options_(ssl_options), type_(type) {
  DCHECK(UsingSSL(type));
  Init(GetHostname(type, ssl_options));
}

BaseTestServer::~BaseTestServer() {}

bool BaseTestServer::Start() {
  return StartInBackground() && BlockUntilStarted();
}

const HostPortPair& BaseTestServer::host_port_pair() const {
  DCHECK(started_);
  return host_port_pair_;
}

const base::DictionaryValue& BaseTestServer::server_data() const {
  DCHECK(started_);
  DCHECK(server_data_.get());
  return *server_data_;
}

std::string BaseTestServer::GetScheme() const {
  switch (type_) {
    case TYPE_FTP:
      return "ftp";
    case TYPE_HTTP:
      return "http";
    case TYPE_HTTPS:
      return "https";
    case TYPE_WS:
      return "ws";
    case TYPE_WSS:
      return "wss";
    case TYPE_TCP_ECHO:
    case TYPE_UDP_ECHO:
    default:
      NOTREACHED();
  }
  return std::string();
}

bool BaseTestServer::GetAddressList(AddressList* address_list) const {
  DCHECK(address_list);

  std::unique_ptr<HostResolver> resolver(
      HostResolver::CreateDefaultResolver(NULL));
  HostResolver::RequestInfo info(host_port_pair_);
  // Limit the lookup to IPv4. When started with the default
  // address of kLocalhost, testserver.py only supports IPv4.
  // If a custom hostname is used, it's possible that the test
  // server will listen on both IPv4 and IPv6, so this will
  // still work. The testserver does not support explicit
  // IPv6 literal hostnames.
  info.set_address_family(ADDRESS_FAMILY_IPV4);
  TestCompletionCallback callback;
  std::unique_ptr<HostResolver::Request> request;
  int rv = resolver->Resolve(info, DEFAULT_PRIORITY, address_list,
                             callback.callback(), &request, NetLogWithSource());
  if (rv == ERR_IO_PENDING)
    rv = callback.WaitForResult();
  if (rv != OK) {
    LOG(ERROR) << "Failed to resolve hostname: " << host_port_pair_.host();
    return false;
  }
  return true;
}

uint16_t BaseTestServer::GetPort() {
  return host_port_pair_.port();
}

void BaseTestServer::SetPort(uint16_t port) {
  host_port_pair_.set_port(port);
}

GURL BaseTestServer::GetURL(const std::string& path) const {
  return GURL(GetScheme() + "://" + host_port_pair_.ToString() + "/" + path);
}

GURL BaseTestServer::GetURLWithUser(const std::string& path,
                                const std::string& user) const {
  return GURL(GetScheme() + "://" + user + "@" + host_port_pair_.ToString() +
              "/" + path);
}

GURL BaseTestServer::GetURLWithUserAndPassword(const std::string& path,
                                           const std::string& user,
                                           const std::string& password) const {
  return GURL(GetScheme() + "://" + user + ":" + password + "@" +
              host_port_pair_.ToString() + "/" + path);
}

// static
bool BaseTestServer::GetFilePathWithReplacements(
    const std::string& original_file_path,
    const std::vector<StringPair>& text_to_replace,
    std::string* replacement_path) {
  std::string new_file_path = original_file_path;
  bool first_query_parameter = true;
  const std::vector<StringPair>::const_iterator end = text_to_replace.end();
  for (std::vector<StringPair>::const_iterator it = text_to_replace.begin();
       it != end;
       ++it) {
    const std::string& old_text = it->first;
    const std::string& new_text = it->second;
    std::string base64_old;
    std::string base64_new;
    base::Base64Encode(old_text, &base64_old);
    base::Base64Encode(new_text, &base64_new);
    if (first_query_parameter) {
      new_file_path += "?";
      first_query_parameter = false;
    } else {
      new_file_path += "&";
    }
    new_file_path += "replace_text=";
    new_file_path += base64_old;
    new_file_path += ":";
    new_file_path += base64_new;
  }

  *replacement_path = new_file_path;
  return true;
}

bool BaseTestServer::LoadTestRootCert() const {
  TestRootCerts* root_certs = TestRootCerts::GetInstance();
  if (!root_certs)
    return false;

  // Should always use absolute path to load the root certificate.
  base::FilePath root_certificate_path;
  if (!GetLocalCertificatesDir(certificates_dir_, &root_certificate_path))
    return false;

  if (ssl_options_.server_certificate == SSLOptions::CERT_AUTO ||
      ssl_options_.server_certificate ==
          SSLOptions::CERT_AUTO_AIA_INTERMEDIATE) {
    return root_certs->AddFromFile(
        root_certificate_path.AppendASCII("ocsp-test-root.pem"));
  } else {
    return root_certs->AddFromFile(
        root_certificate_path.AppendASCII("root_ca_cert.pem"));
  }
}

scoped_refptr<X509Certificate> BaseTestServer::GetCertificate() const {
  base::FilePath certificate_path;
  if (!GetLocalCertificatesDir(certificates_dir_, &certificate_path))
    return nullptr;

  base::FilePath certificate_file(ssl_options_.GetCertificateFile());
  if (certificate_file.value().empty())
    return nullptr;

  certificate_path = certificate_path.Append(certificate_file);

  std::string cert_data;
  if (!base::ReadFileToString(certificate_path, &cert_data))
    return nullptr;

  CertificateList certs_in_file =
      X509Certificate::CreateCertificateListFromBytes(
          cert_data.data(), cert_data.size(),
          X509Certificate::FORMAT_PEM_CERT_SEQUENCE);
  if (certs_in_file.empty())
    return nullptr;
  return certs_in_file[0];
}

void BaseTestServer::Init(const std::string& host) {
  host_port_pair_ = HostPortPair(host, 0);

  // TODO(battre) Remove this after figuring out why the TestServer is flaky.
  // http://crbug.com/96594
  log_to_console_ = true;
}

void BaseTestServer::SetResourcePath(const base::FilePath& document_root,
                                     const base::FilePath& certificates_dir) {
  // This method shouldn't get called twice.
  DCHECK(certificates_dir_.empty());
  document_root_ = document_root;
  certificates_dir_ = certificates_dir;
  DCHECK(!certificates_dir_.empty());
}

bool BaseTestServer::SetAndParseServerData(const std::string& server_data,
                                           int* port) {
  VLOG(1) << "Server data: " << server_data;
  base::JSONReader json_reader;
  std::unique_ptr<base::Value> value(json_reader.ReadToValue(server_data));
  if (!value.get() || !value->IsType(base::Value::Type::DICTIONARY)) {
    LOG(ERROR) << "Could not parse server data: "
               << json_reader.GetErrorMessage();
    return false;
  }

  server_data_.reset(static_cast<base::DictionaryValue*>(value.release()));
  if (!server_data_->GetInteger("port", port)) {
    LOG(ERROR) << "Could not find port value";
    return false;
  }
  if ((*port <= 0) || (*port > std::numeric_limits<uint16_t>::max())) {
    LOG(ERROR) << "Invalid port value: " << port;
    return false;
  }

  return true;
}

bool BaseTestServer::SetupWhenServerStarted() {
  DCHECK(host_port_pair_.port());
  DCHECK(!started_);

  if (UsingSSL(type_) && !LoadTestRootCert())
      return false;

  started_ = true;
  allowed_port_.reset(new ScopedPortException(host_port_pair_.port()));
  return true;
}

void BaseTestServer::CleanUpWhenStoppingServer() {
  TestRootCerts* root_certs = TestRootCerts::GetInstance();
  root_certs->Clear();

  host_port_pair_.set_port(0);
  allowed_port_.reset();
  started_ = false;
}

// Generates a dictionary of arguments to pass to the Python test server via
// the test server spawner, in the form of
// { argument-name: argument-value, ... }
// Returns false if an invalid configuration is specified.
bool BaseTestServer::GenerateArguments(base::DictionaryValue* arguments) const {
  DCHECK(arguments);

  arguments->SetString("host", host_port_pair_.host());
  arguments->SetInteger("port", host_port_pair_.port());
  arguments->SetString("data-dir", document_root_.value());

  if (VLOG_IS_ON(1) || log_to_console_)
    arguments->Set("log-to-console", std::make_unique<base::Value>());

  if (ws_basic_auth_) {
    DCHECK(type_ == TYPE_WS || type_ == TYPE_WSS);
    arguments->Set("ws-basic-auth", std::make_unique<base::Value>());
  }

  if (no_anonymous_ftp_user_) {
    DCHECK_EQ(TYPE_FTP, type_);
    arguments->Set("no-anonymous-ftp-user", std::make_unique<base::Value>());
  }

  if (UsingSSL(type_)) {
    // Check the certificate arguments of the HTTPS server.
    base::FilePath certificate_path(certificates_dir_);
    base::FilePath certificate_file(ssl_options_.GetCertificateFile());
    if (!certificate_file.value().empty()) {
      certificate_path = certificate_path.Append(certificate_file);
      if (certificate_path.IsAbsolute() &&
          !base::PathExists(certificate_path)) {
        LOG(ERROR) << "Certificate path " << certificate_path.value()
                   << " doesn't exist. Can't launch https server.";
        return false;
      }
      arguments->SetString("cert-and-key-file", certificate_path.value());
    }

    // Check the client certificate related arguments.
    if (ssl_options_.request_client_certificate)
      arguments->Set("ssl-client-auth", std::make_unique<base::Value>());
    std::unique_ptr<base::ListValue> ssl_client_certs(new base::ListValue());

    std::vector<base::FilePath>::const_iterator it;
    for (it = ssl_options_.client_authorities.begin();
         it != ssl_options_.client_authorities.end(); ++it) {
      if (it->IsAbsolute() && !base::PathExists(*it)) {
        LOG(ERROR) << "Client authority path " << it->value()
                   << " doesn't exist. Can't launch https server.";
        return false;
      }
      ssl_client_certs->AppendString(it->value());
    }

    if (ssl_client_certs->GetSize())
      arguments->Set("ssl-client-ca", std::move(ssl_client_certs));

    std::unique_ptr<base::ListValue> client_cert_types(new base::ListValue());
    for (size_t i = 0; i < ssl_options_.client_cert_types.size(); i++) {
      client_cert_types->AppendString(
          GetClientCertType(ssl_options_.client_cert_types[i]));
    }
    if (client_cert_types->GetSize())
      arguments->Set("ssl-client-cert-type", std::move(client_cert_types));
  }

  if (type_ == TYPE_HTTPS) {
    arguments->Set("https", std::make_unique<base::Value>());

    if (ssl_options_.server_certificate ==
        SSLOptions::CERT_AUTO_AIA_INTERMEDIATE)
      arguments->Set("aia-intermediate", std::make_unique<base::Value>());

    std::string ocsp_arg = ssl_options_.GetOCSPArgument();
    if (!ocsp_arg.empty())
      arguments->SetString("ocsp", ocsp_arg);

    std::string ocsp_date_arg = ssl_options_.GetOCSPDateArgument();
    if (!ocsp_date_arg.empty())
      arguments->SetString("ocsp-date", ocsp_date_arg);

    std::string ocsp_produced_arg = ssl_options_.GetOCSPProducedArgument();
    if (!ocsp_produced_arg.empty())
      arguments->SetString("ocsp-produced", ocsp_produced_arg);

    if (ssl_options_.cert_serial != 0) {
      arguments->SetInteger("cert-serial", ssl_options_.cert_serial);
    }

    // Check key exchange argument.
    std::unique_ptr<base::ListValue> key_exchange_values(new base::ListValue());
    GetKeyExchangesList(ssl_options_.key_exchanges, key_exchange_values.get());
    if (key_exchange_values->GetSize())
      arguments->Set("ssl-key-exchange", std::move(key_exchange_values));
    // Check bulk cipher argument.
    std::unique_ptr<base::ListValue> bulk_cipher_values(new base::ListValue());
    GetCiphersList(ssl_options_.bulk_ciphers, bulk_cipher_values.get());
    if (bulk_cipher_values->GetSize())
      arguments->Set("ssl-bulk-cipher", std::move(bulk_cipher_values));
    if (ssl_options_.record_resume)
      arguments->Set("https-record-resume", std::make_unique<base::Value>());
    if (ssl_options_.tls_intolerant != SSLOptions::TLS_INTOLERANT_NONE) {
      arguments->SetInteger("tls-intolerant", ssl_options_.tls_intolerant);
      arguments->Set("tls-intolerance-type", GetTLSIntoleranceType(
          ssl_options_.tls_intolerance_type));
    }
    if (ssl_options_.fallback_scsv_enabled)
      arguments->Set("fallback-scsv", std::make_unique<base::Value>());
    if (!ssl_options_.signed_cert_timestamps_tls_ext.empty()) {
      std::string b64_scts_tls_ext;
      base::Base64Encode(ssl_options_.signed_cert_timestamps_tls_ext,
                         &b64_scts_tls_ext);
      arguments->SetString("signed-cert-timestamps-tls-ext", b64_scts_tls_ext);
    }
    if (ssl_options_.staple_ocsp_response)
      arguments->Set("staple-ocsp-response", std::make_unique<base::Value>());
    if (ssl_options_.ocsp_server_unavailable) {
      arguments->Set("ocsp-server-unavailable",
                     std::make_unique<base::Value>());
    }
    if (!ssl_options_.alpn_protocols.empty()) {
      std::unique_ptr<base::ListValue> alpn_protocols(new base::ListValue());
      for (const std::string& proto : ssl_options_.alpn_protocols) {
        alpn_protocols->AppendString(proto);
      }
      arguments->Set("alpn-protocols", std::move(alpn_protocols));
    }
    if (!ssl_options_.npn_protocols.empty()) {
      std::unique_ptr<base::ListValue> npn_protocols(new base::ListValue());
      for (const std::string& proto : ssl_options_.npn_protocols) {
        npn_protocols->AppendString(proto);
      }
      arguments->Set("npn-protocols", std::move(npn_protocols));
    }
    if (ssl_options_.alert_after_handshake)
      arguments->Set("alert-after-handshake", std::make_unique<base::Value>());

    if (ssl_options_.disable_channel_id)
      arguments->Set("disable-channel-id", std::make_unique<base::Value>());
    if (ssl_options_.disable_extended_master_secret) {
      arguments->Set("disable-extended-master-secret",
                     std::make_unique<base::Value>());
    }
    if (!ssl_options_.supported_token_binding_params.empty()) {
      std::unique_ptr<base::ListValue> token_binding_params(
          new base::ListValue());
      arguments->Set(
          "token-binding-params",
          GetTokenBindingParams(ssl_options_.supported_token_binding_params));
    }
  }

  return GenerateAdditionalArguments(arguments);
}

bool BaseTestServer::GenerateAdditionalArguments(
    base::DictionaryValue* arguments) const {
  return true;
}

}  // namespace net
