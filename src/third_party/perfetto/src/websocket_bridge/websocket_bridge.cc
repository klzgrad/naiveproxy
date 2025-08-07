/*
 * Copyright (C) 2021 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "src/websocket_bridge/websocket_bridge.h"

#include <stdint.h>

#include <cstdlib>
#include <map>
#include <memory>
#include <vector>

#include "perfetto/ext/base/http/http_server.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/unix_socket.h"
#include "perfetto/ext/base/unix_task_runner.h"
#include "perfetto/tracing/default_socket.h"

namespace perfetto {
namespace {

constexpr int kWebsocketPort = 8037;

struct Endpoint {
  const char* uri;
  const char* endpoint;
  base::SockFamily family;
};

class WSBridge : public base::HttpRequestHandler,
                 public base::UnixSocket::EventListener {
 public:
  void Main(int argc, char** argv);

  // base::HttpRequestHandler implementation.
  void OnHttpRequest(const base::HttpRequest&) override;
  void OnWebsocketMessage(const base::WebsocketMessage&) override;
  void OnHttpConnectionClosed(base::HttpServerConnection*) override;

  // base::UnixSocket::EventListener implementation.
  void OnNewIncomingConnection(base::UnixSocket*,
                               std::unique_ptr<base::UnixSocket>) override;
  void OnConnect(base::UnixSocket*, bool) override;
  void OnDisconnect(base::UnixSocket*) override;
  void OnDataAvailable(base::UnixSocket* self) override;

 private:
  base::HttpServerConnection* GetWebsocket(base::UnixSocket*);

  base::UnixTaskRunner task_runner_;
  std::vector<Endpoint> endpoints_;
  std::map<base::HttpServerConnection*, std::unique_ptr<base::UnixSocket>>
      conns_;
};

void WSBridge::Main(int, char**) {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  // On Windows traced used a TCP socket.
  const auto kTracedFamily = base::SockFamily::kInet;
#else
  const auto kTracedFamily = base::SockFamily::kUnix;
#endif
  // The ADB_SERVER_SOCKET environment variable is sourced from
  // the commandline.cpp file in the ADB module of the Android platform.
  // Examples: tcp:localhost:5037 or tcp:10.52.8.53:5037.
  std::string adb_socket_endpoint;
  if (const char* adb_ss = getenv("ADB_SERVER_SOCKET"); adb_ss) {
    base::StringView adb_ss_sv(adb_ss);
    adb_socket_endpoint = base::StripPrefix(adb_ss, "tcp:");

    // Ensure that ADB_SERVER_SOCKET actually starts with tcp:
    PERFETTO_CHECK(adb_socket_endpoint.size() != adb_ss_sv.size());
  } else {
    adb_socket_endpoint = "127.0.0.1:5037";
  }
  PERFETTO_LOG("[WSBridge] adb server socket is:%s.",
               adb_socket_endpoint.c_str());
  endpoints_.push_back({"/traced", GetConsumerSocket(), kTracedFamily});
  endpoints_.push_back(
      {"/adb", adb_socket_endpoint.c_str(), base::SockFamily::kInet});

  base::HttpServer srv(&task_runner_, this);
  srv.AddAllowedOrigin("http://localhost:10000");
  srv.AddAllowedOrigin("http://127.0.0.1:10000");
  srv.AddAllowedOrigin("https://ui.perfetto.dev");

  srv.Start("localhost", kWebsocketPort);
  PERFETTO_LOG("[WSBridge] Listening on 127.0.0.1:%d", kWebsocketPort);
  task_runner_.Run();
}

void WSBridge::OnHttpRequest(const base::HttpRequest& req) {
  for (const auto& ep : endpoints_) {
    if (req.uri != ep.uri || !req.is_websocket_handshake)
      continue;

    // Connect to the endpoint in blocking mode.
    auto sock_raw =
        base::UnixSocketRaw::CreateMayFail(ep.family, base::SockType::kStream);
    if (!sock_raw) {
      PERFETTO_PLOG("socket() failed");
      req.conn->SendResponseAndClose("500 Server Error");
      return;
    }
    PERFETTO_LOG("[WSBridge] New connection from \"%.*s\"",
                 static_cast<int>(req.origin.size()), req.origin.data());
    sock_raw.SetTxTimeout(3000);
    sock_raw.SetBlocking(true);

    if (!sock_raw.Connect(ep.endpoint)) {
      PERFETTO_ELOG("[WSBridge] Connection to %s failed", ep.endpoint);
      req.conn->SendResponseAndClose("503 Service Unavailable");
      return;
    }
    sock_raw.SetBlocking(false);

    PERFETTO_DLOG("[WSBridge] Connected to %s", ep.endpoint);
    conns_[req.conn] = base::UnixSocket::AdoptConnected(
        sock_raw.ReleaseFd(), this, &task_runner_, ep.family,
        base::SockType::kStream);

    req.conn->UpgradeToWebsocket(req);
    return;
  }  // for (endpoint)
  req.conn->SendResponseAndClose("404 Not Found");
}

// Called when an inbound websocket message is received from the browser.
void WSBridge::OnWebsocketMessage(const base::WebsocketMessage& msg) {
  auto it = conns_.find(msg.conn);
  PERFETTO_CHECK(it != conns_.end());
  // Pass through the websocket message onto the endpoint TCP socket.
  base::UnixSocket& sock = *it->second;
  sock.Send(msg.data.data(), msg.data.size());
}

// Called when a TCP message is received from the endpoint.
void WSBridge::OnDataAvailable(base::UnixSocket* sock) {
  base::HttpServerConnection* websocket = GetWebsocket(sock);
  PERFETTO_CHECK(websocket);

  char buf[8192];
  auto rsize = sock->Receive(buf, sizeof(buf));
  if (rsize > 0) {
    websocket->SendWebsocketMessage(buf, static_cast<size_t>(rsize));
  } else {
    // Connection closed or errored.
    sock->Shutdown(/*notify=*/true);  // Will trigger OnDisconnect().
    websocket->Close();
  }
}

// Called when the browser terminates the websocket connection.
void WSBridge::OnHttpConnectionClosed(base::HttpServerConnection* websocket) {
  PERFETTO_DLOG("[WSBridge] Websocket connection closed");
  auto it = conns_.find(websocket);
  if (it == conns_.end())
    return;  // Can happen if ADB closed first.
  base::UnixSocket& sock = *it->second;
  sock.Shutdown(/*notify=*/true);
  conns_.erase(websocket);
}

void WSBridge::OnDisconnect(base::UnixSocket* sock) {
  base::HttpServerConnection* websocket = GetWebsocket(sock);
  if (!websocket)
    return;
  websocket->Close();
  sock->Shutdown(/*notify=*/false);
  conns_.erase(websocket);
  PERFETTO_DLOG("[WSBridge] Socket connection closed");
}

base::HttpServerConnection* WSBridge::GetWebsocket(base::UnixSocket* sock) {
  for (const auto& it : conns_) {
    if (it.second.get() == sock) {
      return it.first;
    }
  }
  return nullptr;
}

void WSBridge::OnConnect(base::UnixSocket*, bool) {}
void WSBridge::OnNewIncomingConnection(base::UnixSocket*,
                                       std::unique_ptr<base::UnixSocket>) {}

}  // namespace

int PERFETTO_EXPORT_ENTRYPOINT WebsocketBridgeMain(int argc, char** argv) {
  perfetto::WSBridge ws_bridge;
  ws_bridge.Main(argc, argv);
  return 0;
}

}  // namespace perfetto
