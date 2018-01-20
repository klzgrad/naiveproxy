// Copyright 2025 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_NAIVE_PREAMBLE_GETTER_H_
#define NET_TOOLS_NAIVE_PREAMBLE_GETTER_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "net/base/completion_once_callback.h"
#include "net/base/completion_repeating_callback.h"
#include "net/filter/source_stream.h"
#include "net/socket/client_socket_handle.h"
#include "net/tools/naive/naive_padding_socket.h"
#include "net/tools/naive/naive_protocol.h"
#include "net/tools/naive/naive_proxy_delegate.h"
#include "url/gurl.h"

namespace net {

class ClientSocketHandle;
class HttpNetworkSession;
class IOBuffer;
class NetLogWithSource;
class ProxyInfo;
struct SSLConfig;
class NetworkAnonymizationKey;

class PreambleGetter {
 public:
  PreambleGetter(const ProxyInfo& proxy_info,
                 HttpNetworkSession* session,
                 const NetworkAnonymizationKey& network_anonymization_key,
                 const NetLogWithSource& net_log);
  ~PreambleGetter();
  PreambleGetter(const PreambleGetter&) = delete;
  PreambleGetter& operator=(const PreambleGetter&) = delete;

  int Start(size_t preamble_index, CompletionOnceCallback callback, bool log_url = true);
  void StartOne();

 private:
  enum State {
    STATE_CONNECT_SERVER_COMPLETE,
    STATE_READ,
    STATE_READ_COMPLETE,
    STATE_NONE,
  };

  class PreambleGetterSourceStream;

  struct Request {
    Request();
    ~Request();

    std::string path;
    std::string ext;
    State next_state = STATE_NONE;
    std::unique_ptr<ClientSocketHandle> server_socket_handle =
        std::make_unique<ClientSocketHandle>();
    std::unique_ptr<SourceStream> upstream;
    scoped_refptr<IOBuffer> read_buffer;
    CompletionOnceCallback callback;
  };

  void OnIOComplete(size_t preamble_index, int result);
  int DoLoop(size_t preamble_index, int last_io_result);
  int DoConnectServerComplete(size_t preamble_index, int result);
  void DoCallback(size_t preamble_index, int result);
  int DoRead(size_t preamble_index);
  int DoReadComplete(size_t preamble_index, int result);
  NaiveProxyDelegate* naive_proxy_delegate() const;

  void AddRootHeaders(HttpRequestHeaders& headers);
  void AddHeaders(const std::string& path, const std::string& ext,HttpRequestHeaders& headers);

  const ProxyInfo& proxy_info_;
  const ProxyServer* proxy_server_;
  HttpNetworkSession* session_;
  const NetworkAnonymizationKey& network_anonymization_key_;
  const NetLogWithSource& net_log_;

  std::vector<std::unique_ptr<Request>> requests_;
  GURL root_;
  std::string sec_ch_ua_;
  std::string sec_ch_ua_mobile_;
  std::string sec_ch_ua_platform_;
  std::string user_agent_;

  base::WeakPtrFactory<PreambleGetter> weak_ptr_factory_{this};
};

}  // namespace net
#endif  // NET_TOOLS_NAIVE_URL_GETTER_H_
