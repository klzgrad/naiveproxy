// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/test/embedded_test_server/embedded_test_server.h"

#include <utility>

#include "base/bind.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/message_loop/message_loop_current.h"
#include "base/message_loop/message_pump_type.h"
#include "base/path_service.h"
#include "base/process/process_metrics.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/task/single_thread_task_executor.h"
#include "base/task_runner_util.h"
#include "base/test/bind_test_util.h"
#include "base/threading/thread_restrictions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "crypto/rsa_private_key.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_errors.h"
#include "net/base/port_util.h"
#include "net/cert/internal/extended_key_usage.h"
#include "net/cert/pem.h"
#include "net/cert/test_root_certs.h"
#include "net/log/net_log_source.h"
#include "net/socket/ssl_server_socket.h"
#include "net/socket/stream_socket.h"
#include "net/socket/tcp_server_socket.h"
#include "net/ssl/ssl_info.h"
#include "net/ssl/ssl_server_config.h"
#include "net/test/cert_builder.h"
#include "net/test/cert_test_util.h"
#include "net/test/embedded_test_server/default_handlers.h"
#include "net/test/embedded_test_server/embedded_test_server_connection_listener.h"
#include "net/test/embedded_test_server/http_connection.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "net/test/embedded_test_server/request_handler_util.h"
#include "net/test/test_data_directory.h"
#include "third_party/boringssl/src/include/openssl/bytestring.h"
#include "third_party/boringssl/src/include/openssl/evp.h"
#include "third_party/boringssl/src/include/openssl/rsa.h"

namespace net {
namespace test_server {

namespace {

std::unique_ptr<HttpResponse> ServeResponseForPath(
    const std::string& expected_path,
    HttpStatusCode status_code,
    const std::string& content_type,
    const std::string& content,
    const HttpRequest& request) {
  if (request.GetURL().path() != expected_path)
    return nullptr;

  auto http_response = std::make_unique<BasicHttpResponse>();
  http_response->set_code(status_code);
  http_response->set_content_type(content_type);
  http_response->set_content(content);
  return http_response;
}

}  // namespace

EmbeddedTestServer::EmbeddedTestServer() : EmbeddedTestServer(TYPE_HTTP) {}

EmbeddedTestServer::EmbeddedTestServer(Type type)
    : is_using_ssl_(type == TYPE_HTTPS),
      connection_listener_(nullptr),
      port_(0),
      cert_(CERT_OK) {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (!is_using_ssl_)
    return;
  RegisterTestCerts();
}

EmbeddedTestServer::~EmbeddedTestServer() {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (Started() && !ShutdownAndWaitUntilComplete()) {
    LOG(ERROR) << "EmbeddedTestServer failed to shut down.";
  }

  {
    base::ScopedAllowBaseSyncPrimitivesForTesting allow_wait_for_thread_join;
    io_thread_.reset();
  }
}

void EmbeddedTestServer::RegisterTestCerts() {
  base::ScopedAllowBlockingForTesting allow_blocking;
  TestRootCerts* root_certs = TestRootCerts::GetInstance();
  bool added_root_certs = root_certs->AddFromFile(GetRootCertPemPath());
  DCHECK(added_root_certs)
      << "Failed to install root cert from EmbeddedTestServer";
}

void EmbeddedTestServer::SetConnectionListener(
    EmbeddedTestServerConnectionListener* listener) {
  DCHECK(!io_thread_.get())
      << "ConnectionListener must be set before starting the server.";
  connection_listener_ = listener;
}

EmbeddedTestServerHandle EmbeddedTestServer::StartAndReturnHandle(int port) {
  if (!Start(port))
    return EmbeddedTestServerHandle();
  return EmbeddedTestServerHandle(this);
}

bool EmbeddedTestServer::Start(int port) {
  bool success = InitializeAndListen(port);
  if (!success)
    return false;
  StartAcceptingConnections();
  return true;
}

bool EmbeddedTestServer::InitializeAndListen(int port) {
  DCHECK(!Started());

  const int max_tries = 5;
  int num_tries = 0;
  bool is_valid_port = false;

  do {
    if (++num_tries > max_tries) {
      LOG(ERROR) << "Failed to listen on a valid port after " << max_tries
                 << " attempts.";
      listen_socket_.reset();
      return false;
    }

    listen_socket_ = std::make_unique<TCPServerSocket>(nullptr, NetLogSource());

    int result =
        listen_socket_->ListenWithAddressAndPort("127.0.0.1", port, 10);
    if (result) {
      LOG(ERROR) << "Listen failed: " << ErrorToString(result);
      listen_socket_.reset();
      return false;
    }

    result = listen_socket_->GetLocalAddress(&local_endpoint_);
    if (result != OK) {
      LOG(ERROR) << "GetLocalAddress failed: " << ErrorToString(result);
      listen_socket_.reset();
      return false;
    }

    port_ = local_endpoint_.port();
    is_valid_port |= net::IsPortAllowedForScheme(
        port_, is_using_ssl_ ? url::kHttpsScheme : url::kHttpScheme);
  } while (!is_valid_port);

  if (is_using_ssl_) {
    base_url_ = GURL("https://" + local_endpoint_.ToString());
    if (cert_ == CERT_MISMATCHED_NAME || cert_ == CERT_COMMON_NAME_IS_DOMAIN) {
      base_url_ = GURL(
          base::StringPrintf("https://localhost:%d", local_endpoint_.port()));
    }
  } else {
    base_url_ = GURL("http://" + local_endpoint_.ToString());
  }

  listen_socket_->DetachFromThread();

  if (is_using_ssl_ && !InitializeSSLServerContext())
    return false;

  return true;
}

bool EmbeddedTestServer::UsingStaticCert() const {
  return !GetCertificateName().empty();
}

bool EmbeddedTestServer::InitializeCertAndKeyFromFile() {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::FilePath certs_dir(GetTestCertsDirectory());
  std::string cert_name = GetCertificateName();
  if (cert_name.empty())
    return false;

  x509_cert_ = CreateCertificateChainFromFile(certs_dir, cert_name,
                                              X509Certificate::FORMAT_AUTO);
  if (!x509_cert_)
    return false;

  base::FilePath key_path = certs_dir.AppendASCII(cert_name);
  std::string key_string;
  if (!base::ReadFileToString(key_path, &key_string))
    return false;
  std::vector<std::string> headers;
  headers.push_back("PRIVATE KEY");
  PEMTokenizer pem_tokenizer(key_string, headers);
  if (!pem_tokenizer.GetNext())
    return false;

  CBS cbs;
  CBS_init(&cbs, reinterpret_cast<const uint8_t*>(pem_tokenizer.data().data()),
           pem_tokenizer.data().size());
  private_key_.reset(EVP_parse_private_key(&cbs));
  return !!private_key_;
}

bool EmbeddedTestServer::GenerateCertAndKey() {
  // Create AIA server and start listening. Need to have the socket initialized
  // so the URL can be put in the AIA records of the generated certs.
  aia_http_server_ = std::make_unique<EmbeddedTestServer>(TYPE_HTTP);
  if (!aia_http_server_->InitializeAndListen())
    return false;

  base::ScopedAllowBlockingForTesting allow_blocking;
  base::FilePath certs_dir(GetTestCertsDirectory());

  // Load root cert and key:
  scoped_refptr<X509Certificate> root_cert =
      ImportCertFromFile(certs_dir, "root_ca_cert.pem");
  if (!root_cert)
    return false;

  // TODO(mattm): root_ca_cert.pem has the key encoded as RSAPrivateKeyInfo.
  // Change to have it encoded as PrivateKeyInfo so that EVP_parse_private_key
  // can be used?
  base::FilePath key_path = certs_dir.AppendASCII("root_ca_cert.pem");
  std::string key_string;
  if (!base::ReadFileToString(key_path, &key_string))
    return false;
  std::vector<std::string> headers;
  headers.push_back("RSA PRIVATE KEY");
  PEMTokenizer pem_tokenizer(key_string, headers);
  if (!pem_tokenizer.GetNext())
    return false;
  bssl::UniquePtr<EVP_PKEY> root_private_key(EVP_PKEY_new());
  if (!root_private_key)
    return false;
  bssl::UniquePtr<RSA> rsa_key(RSA_private_key_from_bytes(
      reinterpret_cast<const uint8_t*>(pem_tokenizer.data().data()),
      pem_tokenizer.data().size()));
  if (!rsa_key)
    return false;
  if (!EVP_PKEY_set1_RSA(root_private_key.get(), rsa_key.get()))
    return false;

  std::unique_ptr<CertBuilder> static_root = CertBuilder::FromStaticCert(
      root_cert->cert_buffer(), root_private_key.get());

  CertificateList orig_leaf_and_intermediate = CreateCertificateListFromFile(
      certs_dir, "ok_cert_by_intermediate.pem", X509Certificate::FORMAT_AUTO);
  if (orig_leaf_and_intermediate.size() != 2)
    return false;

  // Generate intermediate:
  CertBuilder intermediate(orig_leaf_and_intermediate[1]->cert_buffer(),
                           static_root.get());
  std::string intermediate_serial_text =
      base::NumberToString(intermediate.GetSerialNumber());
  std::string intermediate_der = intermediate.GetDER();

  // Generate leaf:
  CertBuilder leaf(orig_leaf_and_intermediate[0]->cert_buffer(), &intermediate);
  std::string ca_issuers_path = "/ca_issuers/" + intermediate_serial_text;
  leaf.SetCaIssuersUrl(aia_http_server_->GetURL(ca_issuers_path));

  // Setup AIA server to serve the intermediate referred to by the leaf.
  aia_http_server_->RegisterRequestHandler(
      base::BindRepeating(ServeResponseForPath, ca_issuers_path, HTTP_OK,
                          "application/pkix-cert", intermediate_der));

  // Server certificate chain does not include the intermediate.
  x509_cert_ = leaf.GetX509Certificate();
  private_key_ = bssl::UpRef(leaf.GetKey());

  // If this server is already accepting connections but is being reconfigured,
  // start the new AIA server now. Otherwise, wait until
  // StartAcceptingConnections so that this server and the AIA server start at
  // the same time. (If the test only called InitializeAndListen they expect no
  // threads to be created yet.)
  if (io_thread_.get())
    aia_http_server_->StartAcceptingConnections();

  return true;
}

bool EmbeddedTestServer::InitializeSSLServerContext() {
  if (UsingStaticCert()) {
    if (!InitializeCertAndKeyFromFile())
      return false;
  } else {
    if (!GenerateCertAndKey())
      return false;
  }
  context_ =
      CreateSSLServerContext(x509_cert_.get(), private_key_.get(), ssl_config_);
  return true;
}

void EmbeddedTestServer::StartAcceptingConnections() {
  DCHECK(Started());
  DCHECK(!io_thread_.get())
      << "Server must not be started while server is running";

  if (aia_http_server_)
    aia_http_server_->StartAcceptingConnections();

  base::Thread::Options thread_options;
  thread_options.message_pump_type = base::MessagePumpType::IO;
  io_thread_ = std::make_unique<base::Thread>("EmbeddedTestServer IO Thread");
  CHECK(io_thread_->StartWithOptions(thread_options));
  CHECK(io_thread_->WaitUntilThreadStarted());

  io_thread_->task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&EmbeddedTestServer::DoAcceptLoop,
                                base::Unretained(this)));
}

bool EmbeddedTestServer::ShutdownAndWaitUntilComplete() {
  DCHECK(thread_checker_.CalledOnValidThread());

  return PostTaskToIOThreadAndWait(base::BindOnce(
      &EmbeddedTestServer::ShutdownOnIOThread, base::Unretained(this)));
}

// static
base::FilePath EmbeddedTestServer::GetRootCertPemPath() {
  return GetTestCertsDirectory().AppendASCII("root_ca_cert.pem");
}

void EmbeddedTestServer::ShutdownOnIOThread() {
  DCHECK(io_thread_->task_runner()->BelongsToCurrentThread());
  weak_factory_.InvalidateWeakPtrs();
  listen_socket_.reset();
  connections_.clear();
}

void EmbeddedTestServer::HandleRequest(HttpConnection* connection,
                                       std::unique_ptr<HttpRequest> request) {
  DCHECK(io_thread_->task_runner()->BelongsToCurrentThread());
  request->base_url = base_url_;

  SSLInfo ssl_info;
  if (connection->socket_->GetSSLInfo(&ssl_info) &&
      ssl_info.early_data_received) {
    request->headers["Early-Data"] = "1";
  }

  for (const auto& monitor : request_monitors_)
    monitor.Run(*request);

  std::unique_ptr<HttpResponse> response;

  for (const auto& handler : request_handlers_) {
    response = handler.Run(*request);
    if (response)
      break;
  }

  if (!response) {
    for (const auto& handler : default_request_handlers_) {
      response = handler.Run(*request);
      if (response)
        break;
    }
  }

  if (!response) {
    LOG(WARNING) << "Request not handled. Returning 404: "
                 << request->relative_url;
    auto not_found_response = std::make_unique<BasicHttpResponse>();
    not_found_response->set_code(HTTP_NOT_FOUND);
    response = std::move(not_found_response);
  }

  response->SendResponse(
      base::BindRepeating(&HttpConnection::SendResponseBytes,
                          connection->GetWeakPtr()),
      base::BindOnce(&EmbeddedTestServer::DidClose, weak_factory_.GetWeakPtr(),
                     connection));
}

GURL EmbeddedTestServer::GetURL(const std::string& relative_url) const {
  DCHECK(Started()) << "You must start the server first.";
  DCHECK(base::StartsWith(relative_url, "/", base::CompareCase::SENSITIVE))
      << relative_url;
  return base_url_.Resolve(relative_url);
}

GURL EmbeddedTestServer::GetURL(
    const std::string& hostname,
    const std::string& relative_url) const {
  GURL local_url = GetURL(relative_url);
  GURL::Replacements replace_host;
  replace_host.SetHostStr(hostname);
  return local_url.ReplaceComponents(replace_host);
}

void EmbeddedTestServer::SetSSLConfig(ServerCertificate cert,
                                      const SSLServerConfig& ssl_config) {
  DCHECK(!Started());
  cert_ = cert;
  x509_cert_ = nullptr;
  private_key_ = nullptr;
  ssl_config_ = ssl_config;
}

bool EmbeddedTestServer::GetAddressList(AddressList* address_list) const {
  *address_list = AddressList(local_endpoint_);
  return true;
}

std::string EmbeddedTestServer::GetIPLiteralString() const {
  return local_endpoint_.address().ToString();
}

bool EmbeddedTestServer::ResetSSLConfigOnIOThread(
    ServerCertificate cert,
    const SSLServerConfig& ssl_config) {
  cert_ = cert;
  ssl_config_ = ssl_config;
  connections_.clear();
  return InitializeSSLServerContext();
}

bool EmbeddedTestServer::ResetSSLConfig(ServerCertificate cert,
                                        const SSLServerConfig& ssl_config) {
  return PostTaskToIOThreadAndWaitWithResult(
      base::BindOnce(&EmbeddedTestServer::ResetSSLConfigOnIOThread,
                     base::Unretained(this), cert, ssl_config));
}

void EmbeddedTestServer::SetSSLConfig(ServerCertificate cert) {
  SetSSLConfig(cert, SSLServerConfig());
}

std::string EmbeddedTestServer::GetCertificateName() const {
  DCHECK(is_using_ssl_);
  switch (cert_) {
    case CERT_OK:
    case CERT_MISMATCHED_NAME:
      return "ok_cert.pem";
    case CERT_COMMON_NAME_IS_DOMAIN:
      return "localhost_cert.pem";
    case CERT_EXPIRED:
      return "expired_cert.pem";
    case CERT_CHAIN_WRONG_ROOT:
      // This chain uses its own dedicated test root certificate to avoid
      // side-effects that may affect testing.
      return "redundant-server-chain.pem";
    case CERT_COMMON_NAME_ONLY:
      return "common_name_only.pem";
    case CERT_SHA1_LEAF:
      return "sha1_leaf.pem";
    case CERT_OK_BY_INTERMEDIATE:
      return "ok_cert_by_intermediate.pem";
    case CERT_BAD_VALIDITY:
      return "bad_validity.pem";
    case CERT_TEST_NAMES:
      return "test_names.pem";
    case CERT_AUTO_AIA_INTERMEDIATE:
      return std::string();
  }

  return "ok_cert.pem";
}

scoped_refptr<X509Certificate> EmbeddedTestServer::GetCertificate() {
  DCHECK(is_using_ssl_);
  if (!x509_cert_) {
    // Some tests want to get the certificate before the server has been
    // initialized, so load it now if necessary. This is only possible if using
    // a static certificate.
    // TODO(mattm): change contract to require initializing first in all cases,
    // update callers.
    CHECK(UsingStaticCert());
    // TODO(mattm): change contract to return nullptr on error instead of
    // CHECKing, update callers.
    CHECK(InitializeCertAndKeyFromFile());
  }
  return x509_cert_;
}

void EmbeddedTestServer::ServeFilesFromDirectory(
    const base::FilePath& directory) {
  RegisterDefaultHandler(base::BindRepeating(&HandleFileRequest, directory));
}

void EmbeddedTestServer::ServeFilesFromSourceDirectory(
    const std::string& relative) {
  base::FilePath test_data_dir;
  CHECK(base::PathService::Get(base::DIR_SOURCE_ROOT, &test_data_dir));
  ServeFilesFromDirectory(test_data_dir.AppendASCII(relative));
}

void EmbeddedTestServer::ServeFilesFromSourceDirectory(
    const base::FilePath& relative) {
  base::FilePath test_data_dir;
  CHECK(base::PathService::Get(base::DIR_SOURCE_ROOT, &test_data_dir));
  ServeFilesFromDirectory(test_data_dir.Append(relative));
}

void EmbeddedTestServer::AddDefaultHandlers(const base::FilePath& directory) {
  ServeFilesFromSourceDirectory(directory);
  RegisterDefaultHandlers(this);
}

void EmbeddedTestServer::RegisterRequestHandler(
    const HandleRequestCallback& callback) {
  DCHECK(!io_thread_.get())
      << "Handlers must be registered before starting the server.";
  request_handlers_.push_back(callback);
}

void EmbeddedTestServer::RegisterRequestMonitor(
    const MonitorRequestCallback& callback) {
  DCHECK(!io_thread_.get())
      << "Monitors must be registered before starting the server.";
  request_monitors_.push_back(callback);
}

void EmbeddedTestServer::RegisterDefaultHandler(
    const HandleRequestCallback& callback) {
  DCHECK(!io_thread_.get())
      << "Handlers must be registered before starting the server.";
  default_request_handlers_.push_back(callback);
}

std::unique_ptr<StreamSocket> EmbeddedTestServer::DoSSLUpgrade(
    std::unique_ptr<StreamSocket> connection) {
  DCHECK(io_thread_->task_runner()->BelongsToCurrentThread());

  return context_->CreateSSLServerSocket(std::move(connection));
}

void EmbeddedTestServer::DoAcceptLoop() {
  while (listen_socket_->Accept(
             &accepted_socket_,
             base::BindOnce(&EmbeddedTestServer::OnAcceptCompleted,
                            base::Unretained(this))) == OK) {
    HandleAcceptResult(std::move(accepted_socket_));
  }
}

bool EmbeddedTestServer::FlushAllSocketsAndConnectionsOnUIThread() {
  return PostTaskToIOThreadAndWait(
      base::BindOnce(&EmbeddedTestServer::FlushAllSocketsAndConnections,
                     base::Unretained(this)));
}

void EmbeddedTestServer::FlushAllSocketsAndConnections() {
  connections_.clear();
}

void EmbeddedTestServer::OnAcceptCompleted(int rv) {
  DCHECK_NE(ERR_IO_PENDING, rv);
  HandleAcceptResult(std::move(accepted_socket_));
  DoAcceptLoop();
}

void EmbeddedTestServer::OnHandshakeDone(HttpConnection* connection, int rv) {
  if (connection->socket_->IsConnected())
    ReadData(connection);
  else
    DidClose(connection);
}

void EmbeddedTestServer::HandleAcceptResult(
    std::unique_ptr<StreamSocket> socket) {
  DCHECK(io_thread_->task_runner()->BelongsToCurrentThread());
  if (connection_listener_)
    socket = connection_listener_->AcceptedSocket(std::move(socket));

  if (is_using_ssl_)
    socket = DoSSLUpgrade(std::move(socket));

  std::unique_ptr<HttpConnection> http_connection_ptr =
      std::make_unique<HttpConnection>(
          std::move(socket),
          base::BindRepeating(&EmbeddedTestServer::HandleRequest,
                              base::Unretained(this)));
  HttpConnection* http_connection = http_connection_ptr.get();
  connections_[http_connection->socket_.get()] = std::move(http_connection_ptr);

  if (is_using_ssl_) {
    SSLServerSocket* ssl_socket =
        static_cast<SSLServerSocket*>(http_connection->socket_.get());
    int rv = ssl_socket->Handshake(
        base::BindOnce(&EmbeddedTestServer::OnHandshakeDone,
                       base::Unretained(this), http_connection));
    if (rv != ERR_IO_PENDING)
      OnHandshakeDone(http_connection, rv);
  } else {
    ReadData(http_connection);
  }
}

void EmbeddedTestServer::ReadData(HttpConnection* connection) {
  while (true) {
    int rv = connection->ReadData(
        base::BindOnce(&EmbeddedTestServer::OnReadCompleted,
                       base::Unretained(this), connection));
    if (rv == ERR_IO_PENDING)
      return;
    if (!HandleReadResult(connection, rv))
      return;
  }
}

void EmbeddedTestServer::OnReadCompleted(HttpConnection* connection, int rv) {
  DCHECK_NE(ERR_IO_PENDING, rv);
  if (HandleReadResult(connection, rv))
    ReadData(connection);
}

bool EmbeddedTestServer::HandleReadResult(HttpConnection* connection, int rv) {
  DCHECK(io_thread_->task_runner()->BelongsToCurrentThread());
  if (connection_listener_)
    connection_listener_->ReadFromSocket(*connection->socket_, rv);
  if (rv <= 0) {
    DidClose(connection);
    return false;
  }

  // Once a single complete request has been received, there is no further need
  // for the connection and it may be destroyed once the response has been sent.
  if (connection->ConsumeData(rv))
    return false;

  return true;
}

void EmbeddedTestServer::DidClose(HttpConnection* connection) {
  DCHECK(io_thread_->task_runner()->BelongsToCurrentThread());
  DCHECK(connection);
  DCHECK_EQ(1u, connections_.count(connection->socket_.get()));

  connections_.erase(connection->socket_.get());
}

HttpConnection* EmbeddedTestServer::FindConnection(StreamSocket* socket) {
  DCHECK(io_thread_->task_runner()->BelongsToCurrentThread());

  auto it = connections_.find(socket);
  if (it == connections_.end()) {
    return nullptr;
  }
  return it->second.get();
}

bool EmbeddedTestServer::PostTaskToIOThreadAndWait(base::OnceClosure closure) {
  // Note that PostTaskAndReply below requires
  // base::ThreadTaskRunnerHandle::Get() to return a task runner for posting
  // the reply task. However, in order to make EmbeddedTestServer universally
  // usable, it needs to cope with the situation where it's running on a thread
  // on which a task executor is not (yet) available or as has been destroyed
  // already.
  //
  // To handle this situation, create temporary task executor to support the
  // PostTaskAndReply operation if the current thread has no task executor.
  // TODO(mattm): Is this still necessary/desirable? Try removing this and see
  // if anything breaks.
  std::unique_ptr<base::SingleThreadTaskExecutor> temporary_loop;
  if (!base::MessageLoopCurrent::Get())
    temporary_loop = std::make_unique<base::SingleThreadTaskExecutor>();

  base::RunLoop run_loop;
  if (!io_thread_->task_runner()->PostTaskAndReply(
          FROM_HERE, std::move(closure), run_loop.QuitClosure())) {
    return false;
  }
  run_loop.Run();

  return true;
}

bool EmbeddedTestServer::PostTaskToIOThreadAndWaitWithResult(
    base::OnceCallback<bool()> task) {
  // Note that PostTaskAndReply below requires
  // base::ThreadTaskRunnerHandle::Get() to return a task runner for posting
  // the reply task. However, in order to make EmbeddedTestServer universally
  // usable, it needs to cope with the situation where it's running on a thread
  // on which a task executor is not (yet) available or as has been destroyed
  // already.
  //
  // To handle this situation, create temporary task executor to support the
  // PostTaskAndReply operation if the current thread has no task executor.
  // TODO(mattm): Is this still necessary/desirable? Try removing this and see
  // if anything breaks.
  std::unique_ptr<base::SingleThreadTaskExecutor> temporary_loop;
  if (!base::MessageLoopCurrent::Get())
    temporary_loop = std::make_unique<base::SingleThreadTaskExecutor>();

  base::RunLoop run_loop;
  bool task_result = false;
  if (!base::PostTaskAndReplyWithResult(
          io_thread_->task_runner().get(), FROM_HERE, std::move(task),
          base::BindOnce(base::BindLambdaForTesting([&](bool result) {
            task_result = result;
            run_loop.Quit();
          })))) {
    return false;
  }
  run_loop.Run();

  return task_result;
}

EmbeddedTestServerHandle::EmbeddedTestServerHandle(
    EmbeddedTestServerHandle&& other) {
  operator=(std::move(other));
}

EmbeddedTestServerHandle& EmbeddedTestServerHandle::operator=(
    EmbeddedTestServerHandle&& other) {
  EmbeddedTestServerHandle temporary;
  std::swap(other.test_server_, temporary.test_server_);
  std::swap(temporary.test_server_, test_server_);
  return *this;
}

EmbeddedTestServerHandle::EmbeddedTestServerHandle(
    EmbeddedTestServer* test_server)
    : test_server_(test_server) {}

EmbeddedTestServerHandle::~EmbeddedTestServerHandle() {
  if (test_server_)
    EXPECT_TRUE(test_server_->ShutdownAndWaitUntilComplete());
}

}  // namespace test_server
}  // namespace net
